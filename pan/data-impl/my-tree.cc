/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "article-filter.h"
#include "data-impl.h"
#include "memchunk.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data-impl/header-rules.h"
#include "pan/data/data.h"
#include "pan/data/pan-db.h"
#include "pan/general/time-elapsed.h"
#include <SQLiteCpp/Statement.h>
#include <cassert>
#include <config.h>
#include <cstdio>
#include <functional>
#include <log4cxx/logger.h>
#include <pan/data/article.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>

using namespace pan;

namespace {
log4cxx::LoggerPtr logger = pan::getLogger("article-tree");
}

/****
*****  ArticleTree functions
****/

void DataImpl ::MyTree ::reset_article_view() const
{
  reset_article_transition_tables();
  pan_db.exec("delete from article_view;");
  LOG4CXX_TRACE(logger, "reset article view done ");
}

void DataImpl ::MyTree ::reset_article_transition_tables() const
{
  pan_db.exec("delete from hidden_article;"
              "delete from exposed_article;"
              "delete from reparented_article;");
  LOG4CXX_TRACE(logger, "reset article transition tables done ");
}

void DataImpl::MyTree::set_parent_in_article_view() const
{
  std::string set_parent_id = R"SQL(
    with recursive whole(article_id, parent_id, show, show_parent) as (
      select a.id,  a.parent_id, show,
        (select show from article_view as a_in where a_in.article_id == a.parent_id)
      from article_view as av
      full outer join article as a on av.article_id = a.id
    ),
    v (article_id, show, new_parent_id, show_parent) as (
      select article_id, show, parent_id, show_parent from whole
      union all
      select article_id, show,
        (select w2.parent_id   from whole as w2 where w2.article_id == v.new_parent_id),
        (select w2.show_parent from whole as w2 where w2.article_id == v.new_parent_id)
      from v
      where new_parent_id is not null and (show_parent == 0 or show_parent is null)
      limit 20000000 -- TODO : remove ?
    )
    update article_view set parent_id =
      (case v.show == 1 and v.show_parent == 1 when True then v.new_parent_id else null end)
    from v
    where v.article_id = article_view.article_id
  )SQL";

  auto set_parent_id_st = SQLite::Statement(pan_db, set_parent_id);
  int count = set_parent_id_st.exec();
  LOG4CXX_TRACE(logger,
                "set parent_id in article_view table done with " << count
                                                                 << " rows");
}

void DataImpl ::MyTree ::initialize_article_view() const
{
  TimeElapsed timer;
  LOG4CXX_TRACE(logger, "Initial load on article_view table");
  SQLite::Transaction setup_article_view_transaction(pan_db);

  reset_article_view();

  int count = fill_article_view_from_article(true);
  LOG4CXX_TRACE(
    logger, "init: fill article_view done (" << timer.get_seconds_elapsed() << "s)");

  // second pass to setup parent_id in article view (this needs whole
  // article_view table to compute parent_id)
  set_parent_in_article_view();
  LOG4CXX_TRACE(
    logger, "init: set parent in article_view done (" << timer.get_seconds_elapsed() << "s)");

  setup_article_view_transaction.commit();

  LOG4CXX_TRACE(logger,
                "Initial load on article_view table done with "
                  << count << " articles ("
                  << timer.get_seconds_elapsed() << "s)");
}

// also apply filters to build article_view
int DataImpl ::MyTree ::fill_article_view_from_article(bool init) const
{
  SQLite::Statement init_st(
    pan_db, "update article_view_status set value = ? where key == 'init'");
  init_st.bind(1, init);
  init_st.exec();

  auto c = [](std::string join, std::string where) -> std::string
  {
    // CTE: compute show status of all articles of a group
    // then update article view with show status, create new entry if needed
    // set again parent_id so  set_parent_in_article_view() works as expected
    return R"SQL(
       with f (article_id, parent_id, show) as (
         select article.id, article.parent_id,
    )SQL" + (where.empty() ? "true" : where)
           + R"SQL(
         from article
         join article_group as ag on ag.article_id = article.id
         join `group` as g on ag.group_id = g.id
    )SQL" + join
           + R"SQL(
         where g.name == ?
       )
       insert into article_view (article_id, parent_id, show)
       select distinct article_id, parent_id, show from f where true
       on conflict
       do update set (show)
          = (select f.show from f where f.article_id == article_view.article_id)
    )SQL";
  };

  auto q = _header_filter.get_sql_query(_data, c, _filter);

  // nb of parameters in the statement, not the nb of already bound parameters
  int param_count = q.getBindParameterCount();
  q.bind(param_count, _group);

  return q.exec();
}

// apply function on exposed article in breadth first order.
// Returns the number of exposed articles
int DataImpl ::MyTree ::call_on_exposed_articles (
  std::function<void(Quark msg_id, Quark parent_id)> cb) const
{
  // see https://www.geeksforgeeks.org/hierarchical-data-and-how-to-query-it-in-sql/
  std::string q = R"SQL(
    with recursive hierarchy as (
      select a.id article_id, message_id, av.parent_id
      from article_view as av
	    join article as a on a.id == av.article_id
      join exposed_article as xp on a.id == xp.article_id
      where av.parent_id is null

      union all

      select a.id, a.message_id, av.parent_id
      from article_view as av
	    join article as a on a.id == av.article_id
      join exposed_article as xp on a.id == xp.article_id
      join hierarchy as h
	    where av.parent_id is not null and av.parent_id = h.article_id
	    limit 10000000 -- todo remove ?
    )
    select hierarchy.message_id, parent.message_id
	  from hierarchy
	  left outer join article as parent on hierarchy.parent_id == parent.id
  )SQL";

  SQLite::Statement st(pan_db, q);
  int count(0);
  while (st.executeStep()) {
    Quark msg_id = st.getColumn(0).getText();
    Quark prt_id;
    if (! st.getColumn(1).isNull())
    {
      prt_id = st.getColumn(1).getText();
    }
    count ++;
    cb(msg_id,prt_id);
  }
  return count;
}

// apply function on reparented articles
// Returns the number of reparented articles
int DataImpl ::MyTree ::call_on_reparented_articles(
  std::function<void(Quark msg_id, Quark new_parent_id)> cb) const
{
  std::string q = R"SQL(
    select child.message_id, parent.message_id
    from reparented_article as rp
    join article_view as av on av.article_id == child.id
    join article as child on rp.article_id == child.id
    join article as parent on av.parent_id == parent.id
  )SQL";

  SQLite::Statement st(pan_db, q);
  int count(0);
  while (st.executeStep())
  {
    cb(st.getColumn(0).getText(), st.getColumn(1).getText());
    count++;
  }
  return count;
}

// apply function on hidden articles
// Returns the number of hidden articles
int DataImpl ::MyTree ::call_on_hidden_articles(
  std::function<void(Quark msg_id)> cb) const
{
  std::string q = R"SQL(
    select message_id
    from hidden_article as h
    join article on h.article_id == article.id
  )SQL";

  SQLite::Statement st(pan_db, q);
  int count(0);
  while (st.executeStep()) {
    cb(st.getColumn(0).getText());
    count ++;
  }
  return count;
}

void DataImpl ::MyTree ::update_article_view() const
{
  TimeElapsed timer;
  LOG4CXX_TRACE(logger, "Update article_view");
  SQLite::Transaction setup_article_view_transaction(pan_db);

  // similar to update_article_view, but init is True here
  reset_article_transition_tables();
  LOG4CXX_TRACE(
    logger, "reset article_view done (" << timer.get_seconds_elapsed() << "s)");

  int count = fill_article_view_from_article(false);
  LOG4CXX_TRACE(
    logger, "fill article_view done (" << timer.get_seconds_elapsed() << "s)");

  // second pass to setup parent_id in article view (this needs whole
  // article_view table to compute parent_id)
  set_parent_in_article_view();
  LOG4CXX_TRACE(
    logger, "set_parent in article_view done (" << timer.get_seconds_elapsed() << "s)");

  // transitions tables (like hidden_articles, exposed_articles... )
  // are filled by triggers

  setup_article_view_transaction.commit();

  LOG4CXX_TRACE(logger,
                "Update article_view done with "
                  << count << " articles ("
                  << timer.get_seconds_elapsed() << "s).");
}

void DataImpl ::MyTree ::get_children_sql(
  Quark const &mid, Quark const &group, std::vector<Article> &setme) const
{
  std::string str("select message_id from article "
                  " join article_view as av on av.article_id == article.id"
                  " where show == True and av.parent_id ");
  str +=
    mid.empty() ? "isnull" : "= (select id from article where message_id == ?)";

  LOG4CXX_TRACE(logger, "query on article_view with «" << str << "»");
  SQLite::Statement q(pan_db, str);

  if (! mid.empty())
  {
    q.bind(1, mid);
  }

  int count(0);
  while (q.executeStep())
  {
    std::string msg_id = q.getColumn(0);
    Article a(group, msg_id);
    setme.push_back(a);
    count++;
  }
  LOG4CXX_TRACE(
    logger, "query on article_view table done with " << count << " articles");
}

Article const *DataImpl ::MyTree ::get_parent(Quark const &mid) const
{
  LOG4CXX_WARN(logger, "deprecated function called");
  Article const *parent(nullptr);
  ArticleNode const *parent_node(nullptr);

  nodes_t::const_iterator child_it(_nodes.find(mid));
  if (child_it != _nodes.end())
  {
    parent_node = child_it->second->_parent;
  }
  if (parent_node)
  {
    parent = parent_node->_article;
  }

  return parent;
}

Article const *DataImpl ::MyTree ::get_article(Quark const &mid) const
{
  LOG4CXX_WARN(logger, "deprecated function called");
  nodes_t::const_iterator it(_nodes.find(mid));
  return it == _nodes.end() ? nullptr : it->second->_article;
}

size_t DataImpl ::MyTree ::size() const
{
  return _nodes.size();
}

void DataImpl ::MyTree ::set_rules(RulesInfo const *rules)
{
  LOG4CXX_DEBUG(logger, "set_rules called");
  if (rules)
  {
    //    std::cerr<<"set rules "<<rules<<std::endl;
    _rules = *rules;
  }
  else
  {
    //    std::cerr<<"rules clear my_tree\n";
    _rules.clear();
  }

  _header_rules.apply_rules(_data, _rules, _group, _save_path);
}

void DataImpl ::MyTree ::set_filter(Data::ShowType const show_type,
                                    FilterInfo const *criteria)
{
  TimeElapsed timer;

  // set the filter...
  if (criteria)
  {
    LOG4CXX_DEBUG(logger,
                  "set_filter called on group " << _group << " with criteria «"
                                                << criteria->describe() << "»");
    _filter = *criteria;
  }
  else
  {
    LOG4CXX_DEBUG(logger, "set_filter called with nullptr criteria");
    _filter.clear();
  }
  _show_type = show_type;

  LOG4CXX_TRACE(logger,
                "apply_sql_filter calls apply_filter in "
                  << timer.get_seconds_elapsed() << "s.");
  apply_sql_filter();
  LOG4CXX_DEBUG(
    logger, "apply_sql_filter done in " << timer.get_seconds_elapsed() << "s.");
}

/****
*****  Life Cycle
****/

DataImpl ::MyTree ::MyTree(DataImpl &data_impl,
                           Quark const &group,
                           Quark const &save_path,
                           Data::ShowType const show_type,
                           FilterInfo const *filter,
                           RulesInfo const *rules) :
  _group(group),
  _data(data_impl),
  _save_path(save_path)
{
  LOG4CXX_DEBUG(logger, "creating new MyTree for group " << group);
  _data.ref_group(_group);
  _data._trees.insert(this);

  set_filter(show_type, filter);
  set_rules(rules);
}

DataImpl ::MyTree ::~MyTree()
{
  LOG4CXX_DEBUG(logger, "Destroying MyTree of group " << _group);
  _nodes.clear();
  _data._trees.erase(this);
  _data.unref_group(_group);
}

/****
*****
****/

struct DataImpl ::MyTree ::NodeMidCompare
{
    typedef std::pair<pan::Quark const, pan::DataImpl::ArticleNode *>
      nodes_v_pair;

    bool operator()(ArticleNode const *a, nodes_v_pair const &b) const
    {
      return a->_mid < b.first;
    }

    bool operator()(nodes_v_pair const &a, ArticleNode const *b) const
    {
      return a.first < b->_mid;
    }

    bool operator()(ArticleNode const *a, ArticleNode const *b) const
    {
      return a->_mid < b->_mid;
    }

    bool operator()(Quark const &a, ArticleNode const *b) const
    {
      return a < b->_mid;
    }

    bool operator()(ArticleNode const *a, Quark const &b) const
    {
      return a->_mid < b;
    }
};

void DataImpl ::MyTree ::cache_articles(std::set<Article const *> s)
{
  Queue *queue(_data.get_queue());
  Prefs &prefs(_data.get_prefs());
  bool const action(prefs.get_flag("rules-autocache-mark-read", false));

  Queue::tasks_t tasks;
  ArticleCache &cache(_data.get_cache());
  foreach_const (std::set<Article const *>, s, it)
  {
    if (! _data.is_read(*it))
    {
      tasks.push_back(new TaskArticle(_data,
                                      _data,
                                      **it,
                                      cache,
                                      _data,
                                      action ? TaskArticle::ACTION_TRUE :
                                               TaskArticle::ACTION_FALSE));
    }
  }
  if (! tasks.empty())
  {
    queue->add_tasks(tasks, Queue::BOTTOM);
  }
}

void DataImpl ::MyTree ::download_articles(std::set<Article const *> s)
{
  Queue *queue(_data.get_queue());

  Queue::tasks_t tasks;
  ArticleCache &cache(_data.get_cache());
  Prefs &prefs(_data.get_prefs());
  bool const action(prefs.get_flag("rules-auto-dl-mark-read", false));
  bool const always(prefs.get_flag("mark-downloaded-articles-read", false));

  foreach_const (std::set<Article const *>, s, it)
  {
    if (! _data.is_read(*it))
    {
      tasks.push_back(new TaskArticle(_data,
                                      _data,
                                      **it,
                                      cache,
                                      _data,
                                      always ? TaskArticle::ALWAYS_MARK :
                                      action ? TaskArticle::ACTION_TRUE :
                                               TaskArticle::ACTION_FALSE,
                                      nullptr,
                                      TaskArticle::DECODE,
                                      _save_path));
    }
  }
  if (! tasks.empty())
  {
    queue->add_tasks(tasks, Queue::BOTTOM);
  }
}

void DataImpl ::MyTree ::apply_sql_filter()
{
  LOG4CXX_TRACE(logger, "called (deprecated ?)");
  auto q = _header_filter.get_sql_filter(_data, _show_type, _filter);
  q.bind(q.getBindParameterCount(), _group);

  std::set<Quark> pass, fail;
  while (q.executeStep())
  {
    std::string msg_id = q.getColumn(0);
    bool in = q.getColumn(1).getInt();

    if (in)
    {
      pass.insert(msg_id);
    }
    else
    {
      fail.insert(msg_id);
    }
  }

  // passing articles not in the tree should be added...
  const_nodes_v nodes;
  _data.find_nodes(pass, _data.get_group_headers(_group)->_nodes, nodes);
  add_articles(nodes);

  // failing articles in the tree should be removed...
  remove_articles(fail);
}

// candidates holds GroupHeader's ArticleNodes pointers
// candidates are sorted by Mid (see NodeMidCompare)
void DataImpl ::MyTree ::apply_filter(const_nodes_v const &candidates)
{
  LOG4CXX_WARN(logger, "deprecated function called");
  NodeMidCompare compare;

  const_nodes_v pass;
  const_nodes_v fail;
  pass.reserve(candidates.size());
  fail.reserve(candidates.size());

  // apply the filter to the whole tree
  if (_filter.empty())
  {
    pass = candidates;
  }
  else
  {
    foreach_const (const_nodes_v, candidates, it)
    {
      if (! (*it)->_article)
      {
        continue;
      }
      else if (_data._article_filter.test_article(
                 _data, _filter, *(*it)->_article))
      {
        pass.push_back(*it);
      }
      else
      {
        fail.push_back(*it);
      }
    }
  }
  // std::cerr << LINE_ID << " " << pass.size() << " of "
  //           << (pass.size() + fail.size()) << " articles pass\n";

  //  maybe include threads or subthreads...
  if (_show_type == Data::SHOW_THREADS)
  {
    // add all parent of passed articles
    const_nodes_v passcopy = pass;
    foreach (const_nodes_v, passcopy, it)
    {
      ArticleNode const *n(*it);
      while ((n = n->_parent))
      {
        pass.push_back(n);
      }
    }
    // remove redudant articles
    std::sort(pass.begin(), pass.end(), compare);
    pass.erase(std::unique(pass.begin(), pass.end()), pass.end());
    // std::cerr << LINE_ID << " reduces to " << pass.size() << " threads\n";
  }

  if (_show_type == Data::SHOW_THREADS || _show_type == Data::SHOW_SUBTHREADS)
  {
    // add all descendants of passed articles
    unique_nodes_t d;
    foreach_const (const_nodes_v, pass, it)
    {
      accumulate_descendants(d, *it);
    }
    // std::cerr << LINE_ID << " expands into " << d.size() << " articles\n";

    const_nodes_v fail2;
    pass.clear();
    // re-apply filter on all passed and descendants articles
    foreach_const (unique_nodes_t, d, it)
    {
      Article const *a((*it)->_article);
      if (a->get_score() > -9999
          || _data._article_filter.test_article(_data, _filter, *a))
      {
        pass.push_back(*it); // pass is now sorted by mid because d was too
      }
      else
      {
        fail2.push_back(*it); // fail2 is sorted by mid because d was too
      }
    }

    // fail cleanup: add fail2 and remove duplicates.
    // both are sorted by mid, so set_union will do the job
    const_nodes_v tmp;
    tmp.reserve(fail.size() + fail2.size());
    std::set_union(fail.begin(),
                   fail.end(),
                   fail2.begin(),
                   fail2.end(),
                   inserter(tmp, tmp.begin()),
                   compare);
    fail.swap(tmp);

    // fail cleanup: remove newly-passing articles
    tmp.clear();
    std::set_difference(fail.begin(),
                        fail.end(),
                        pass.begin(),
                        pass.end(),
                        inserter(tmp, tmp.begin()),
                        compare);
    fail.swap(tmp);
    // std::cerr << LINE_ID << ' ' << pass.size() << " of "
    //           << (pass.size() + fail.size())
    //           << " make it past the show-thread block\n";
  }

  // passing articles not in the tree should be added...
  add_articles(pass);

  // failing articles in the tree should be removed...
  quarks_t mids;
  foreach_const (const_nodes_v, fail, it)
  {
    mids.insert(mids.end(), (*it)->_mid);
  }
  remove_articles(mids);
}

void DataImpl ::MyTree ::remove_articles(quarks_t const &mids)
{
  ArticleTree::Diffs diffs;
  std::set<ArticleNode *> parents;

  LOG4CXX_WARN(logger, "deprecated function called");
  // zero out any corresponding nodes in the tree...
  nodes_v nodes;
  _data.find_nodes(mids, _nodes, nodes);
  foreach_const (nodes_v, nodes, it)
  {
    ArticleNode *node(*it);
    Quark const &mid(node->_mid);

    if (node->_article)
    {
      node->_article = nullptr;
      diffs.removed.insert(diffs.removed.end(), mid);
    }

    if (node->_parent)
    {
      parents.insert(node->_parent);
    }
  }

  foreach (std::set<ArticleNode *>, parents, it)
  {
    ArticleNode *parent(*it);
    ArticleNode::children_t kids;
    foreach (ArticleNode::children_t, parent->_children, kit)
    {
      if (! mids.count((*kit)->_mid))
      {
        kids.push_back(*kit);
      }
    }
    parent->_children.swap(kids);
  }

  // reparent any children who need it...
  foreach_const (nodes_t, _nodes, it)
  {
    ArticleNode *n(it->second);

    // if it's an article with a removed parent, reparent to ancestor
    if (n->_article && n->_parent && ! n->_parent->_article)
    {
      // start filling out a diff entry
      Diffs::Reparent r;
      r.message_id = n->_mid;
      r.old_parent = n->_parent->_mid;

      // look for a suitable parent
      ArticleNode *newparent(n->_parent);
      while (newparent && ! newparent->_article)
      {
        newparent = newparent->_parent;
      }

      // reparent the node
      if (newparent)
      {
        newparent->_children.push_front(n);
        r.new_parent = newparent->_mid;
      }
      n->_parent = newparent;

      diffs.reparented.insert(
        diffs.reparented.end(),
        std::pair<Quark, Diffs::Reparent>(r.message_id, r));
    }
  }

  fire_diffs(diffs);

  foreach_const (quarks_t, diffs.removed, it)
  {
    _nodes.erase(*it);
  }
}

// if this node has an article and wasn't already in `descendants',
// then add it and its children.
void DataImpl ::MyTree ::accumulate_descendants(unique_nodes_t &descendants,
                                                ArticleNode const *node) const
{
  LOG4CXX_WARN(logger, "deprecated function called");

  if (node->_article && descendants.insert(node).second || ! node->_article)
  {
    foreach_const (ArticleNode::children_t, node->_children, it)
    {
      accumulate_descendants(descendants, *it);
    }
  }
}

void DataImpl ::MyTree ::articles_changed(quarks_t const &mids,
                                          bool do_refilter)
{
  LOG4CXX_DEBUG(
    logger, "group " << _group << ": " << mids.size() << " articles changed");

  _header_rules.apply_rules(_data, _rules, _group, _save_path);

  const_nodes_v nodes;
  _data.find_nodes(mids, _data.get_group_headers(_group)->_nodes, nodes);

  if (do_refilter)
  {
    apply_filter(nodes);
  }

  // fire an update event for any of those mids in our tree...
  nodes_v my_nodes;
  _data.find_nodes(mids, _nodes, my_nodes);

  if (! my_nodes.empty())
  {
    ArticleTree::Diffs diffs;
    foreach_const (nodes_v, my_nodes, it)
    {
      diffs.changed.insert(diffs.changed.end(), (*it)->_mid);
    }
    fire_diffs(diffs);
  }
}

void DataImpl ::MyTree ::add_articles(quarks_t const &mids)
{
  LOG4CXX_DEBUG(logger, "group " << _group << ": " << mids.size() << "articles added");

  _header_rules.apply_rules(_data, _rules, _group, _save_path);

  const_nodes_v nodes;
  _data.find_nodes(mids, _data.get_group_headers(_group)->_nodes, nodes);
  apply_filter(nodes);
}

struct DataImpl ::MyTree ::TwoNodes
{
    ArticleNode const *node;
    ArticleNode *tree_node;

    TwoNodes(ArticleNode const *n, ArticleNode *t) :
      node(n),
      tree_node(t)
    {
    }
};

void DataImpl ::MyTree ::add_articles(const_nodes_v const &nodes_in)
{
  LOG4CXX_WARN(logger, "deprecated add_articles called");
  //  std::cerr<<"add articles nodes\n";

  NodeMidCompare compare;

  ///
  ///  1. add the new articles
  ///

  // sort the nodes by Message-ID
  // so that we can use `nodes' for set operations
  const_nodes_v nodes(nodes_in);
  std::sort(nodes.begin(), nodes.end(), compare);

  // nodes -= this->_nodes;
  // (don't try to add articles we've already got.)
  if (1)
  {
    const_nodes_v tmp;
    tmp.reserve(nodes.size());
    std::set_difference(nodes.begin(),
                        nodes.end(),
                        _nodes.begin(),
                        _nodes.end(),
                        inserter(tmp, tmp.begin()),
                        compare);
    nodes.swap(tmp);
    // std::cerr << LINE_ID << ' ' << nodes.size() << " unique nodes\n";
  }

  // build new MyTree nodes for each of the articles being added
  nodes_v tree_nodes;
  tree_nodes.reserve(nodes.size());
  foreach_const (const_nodes_v, nodes, it)
  {
    Article const *a((*it)->_article);
    ArticleNode *node(_node_chunk.alloc());
    node->_mid = a->message_id;
    node->_article = const_cast<Article *>(a);
    // std::cerr << LINE_ID << " added " << node->_mid << " to the tree\n";
    std::pair<nodes_t::iterator, bool> result(
      _nodes.insert(std::pair<Quark, ArticleNode *>(node->_mid, node)));
    g_assert(result.second); // freshly added; not a duplicate
    tree_nodes.push_back(node);
  }

  ///
  ///  2. find parents for the new articles
  ///

  ArticleTree::Diffs diffs;
  for (size_t i(0), n(nodes.size()); i != n; ++i)
  {
    ArticleNode const *node(nodes[i]);
    ArticleNode *tree_node(tree_nodes[i]);
    g_assert(node->_mid == tree_node->_mid);

    Diffs::Added added;
    added.message_id = tree_node->_mid;

    // find the first ancestor that's present in our tree
    ArticleNode *parent(nullptr);
    nodes_t::const_iterator const nend(_nodes.end());
    for (ArticleNode const *it(node->_parent); it && ! parent; it = it->_parent)
    {
      nodes_t::iterator nit(_nodes.find(it->_mid));
      if (nit != nend)
      {
        parent = nit->second;
      }
    }

    // if we found a parent, use it.
    if (parent)
    {
      tree_node->_parent = parent;
      parent->_children.push_back(tree_node);
      added.parent = parent->_mid;
    }

    diffs.added.insert(diffs.added.end(),
                       std::pair<Quark, Diffs::Added>(added.message_id, added));
    // std::cerr << LINE_ID << " child " << added.message_id
    //                      << " has parent " << added.parent << std::endl;
  }

  ///
  ///  3. possibly reparent other articles
  ///

  // get a list of all articles that are descendants of the new articles
  const_nodes_v descendants;
  if (1)
  {
    unique_nodes_t tmp;
    foreach (const_nodes_v, nodes, it)
    {
      accumulate_descendants(tmp, *it);
    }
    descendants.assign(tmp.begin(), tmp.end());
  }

  // descendants = descendants - nodes;
  // (we parented `nodes' in step 2.)
  if (1)
  {
    const_nodes_v tmp;
    std::set_difference(descendants.begin(),
                        descendants.end(),
                        nodes.begin(),
                        nodes.end(),
                        inserter(tmp, tmp.begin()));
    descendants.swap(tmp);
  }

  // map the 'canonical' nodes to MyTree nodes...
  typedef std::vector<TwoNodes> twonodes_v;
  twonodes_v descend;
  descend.reserve(descendants.size());
  if (1)
  {
    nodes_t::iterator nit(_nodes.begin()), nend(_nodes.end());
    const_nodes_v::const_iterator dit(descendants.begin()),
      dend(descendants.end());
    while (nit != nend && dit != dend)
    {
      if (nit->second->_mid < (*dit)->_mid)
      {
        ++nit;
      }
      else if ((*dit)->_mid < nit->second->_mid)
      {
        ++dit;
      }
      else
      {
        g_assert(nit->second->_mid == (*dit)->_mid);
        descend.push_back(TwoNodes(*dit, nit->second));
        ++nit;
        ++dit;
      }
    }
  }

  // now walk though the descendants and possibly reparent them.
  foreach_const (twonodes_v, descend, it)
  {
    ArticleNode *tree_node(it->tree_node);
    ArticleNode const *node(it->node);
    g_assert(node->_mid == tree_node->_mid);

    // std::cerr << LINE_ID << " looking for a new parent for "
    //           << tree_node->_mid << std::endl;
    ArticleNode *new_parent(nullptr);
    node = node->_parent;
    while (node && ! new_parent)
    {
      // std::cerr << LINE_ID << " maybe " << node->_mid
      //                      << " can be its new parent..." << std::endl;
      nodes_t::iterator nit(_nodes.find(node->_mid));
      if (nit != _nodes.end())
      {
        new_parent = nit->second;
      }
      else
      {
        node = node->_parent;
        // std::cerr << LINE_ID << " but no, that's not in the tree.\n";
      }
    }
    // std::cerr << LINE_ID << " " << tree_node->_mid << "'s best parent is "
    //           << new_parent << std::endl;

    if (new_parent == tree_node->_parent)
    {
      continue;
    }

    ArticleNode *old_parent(tree_node->_parent);
    ArticleTree::Diffs::Reparent reparent;
    if (old_parent)
    {
      reparent.old_parent = old_parent->_mid;
      old_parent->_children.remove(tree_node);
    }
    new_parent->_children.push_back(tree_node);
    tree_node->_parent = new_parent;
    reparent.message_id = tree_node->_mid;
    reparent.new_parent = new_parent->_mid;
    diffs.reparented.insert(
      diffs.reparented.end(),
      std::pair<Quark, Diffs::Reparent>(reparent.message_id, reparent));
    // std::cerr << LINE_ID << " REPARENTED: " << reparent.message_id
    //           << " has a new parent " << reparent.new_parent << std::endl;
  }

  // std::cerr << LINE_ID << ' ' << _nodes.size() << " articles in the tree\n";
  if (! diffs.reparented.empty() || ! diffs.added.empty())
  {
    fire_diffs(diffs);
  }
}
