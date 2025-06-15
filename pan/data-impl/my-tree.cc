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
#include <cstdint>
#include <cstdio>
#include <functional>
#include <glib.h>
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
  pan_db.exec("delete from article_view;");
  LOG4CXX_TRACE(logger, "reset article view done ");
}

void DataImpl::MyTree::set_parent_in_article_view() const {
  std::string set_parent_id = R"SQL(
    -- Like article with added parent_status column,
    -- status can be null for articles not present in article_view.
    -- This table is used to recursively retrieve parents until
    -- a suitable one is found.
    with recursive
    article_status(article_id, parent_id, to_delete, status, parent_status) as (
      select a.id,  a.parent_id, to_delete,
        (select status from article_view as a_in where a_in.article_id == a.id),
        (select status from article_view as a_in where a_in.article_id == a.parent_id)
      from article as a
    ),
    -- Starting from articles to be shown, retrieve recursively parent_id until
    -- root or a parent with status != hidden is found
    v (count, article_id, new_parent_id, to_delete, status, parent_status) as (
      select 0, article_id, parent_id, to_delete, status, parent_status from article_status
        where status is not null and status is not "h" and not to_delete
      union all
      select count + 1, v.article_id, as2.parent_id, v.to_delete, v.status, as2.parent_status
      from v
      join article_status as as2 on as2.article_id == v.new_parent_id
      where v.parent_status is null or v.parent_status is "h" or v.to_delete
      limit 20000 -- TODO : remove ?
    ),
    -- In v, the same article id may be present several time. Only the last is valid.
    -- v2 filters the result, keeping only the highest count, i.e. the latest found.
    -- See https://sqlite.org/lang_select.html#bare_columns_in_an_aggregate_query for max() usage
    v2 (count, article_id, new_parent_id, status, parent_status) as (
      select max(count),article_id, new_parent_id, status, parent_status from v
      where status is not null and status is not "h" and not to_delete
      group by article_id
    )
   update article_view set parent_id = v2.new_parent_id,
     -- do not set reparented on new articles
     status = (case article_view.status when "s" then "r" else article_view.status end)
   from v2
   where v2.article_id = article_view.article_id and parent_id is not v2.new_parent_id
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

  int count = fill_article_view_from_article();
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
int DataImpl ::MyTree ::fill_article_view_from_article() const
{
  auto c = [](std::string join, std::string where) -> std::string {
    LOG4CXX_TRACE(logger, "SQL query created with join «" << join << "» where «"
                                                          << where);

    // CTE: compute show status of all articles of a group
    // then update article view with show status, create new entry if needed
    // set again parent_id so  set_parent_in_article_view() works as expected
  return R"SQL(
       with f (article_id, parent_id, to_delete) as (
         select article.id, article.parent_id, to_delete
         from article
         join article_group as ag on ag.article_id = article.id
         join `group` as g on ag.group_id = g.id
    )SQL" +
         join + "where " + (where.empty() ? "true" : where) + R"SQL(
         and g.name == ? and not article.to_delete
       )
       insert into article_view (article_id, parent_id, status)
       select distinct article_id, parent_id, "e" from f where not to_delete
       on conflict (article_id)
       do update set mark = False
    )SQL";
  };

  // get a query that takes into account filtered articles
  auto q = _header_filter.get_sql_query(_data, c, _filter);

  // nb of parameters in the statement, not the nb of already bound parameters
  int param_count = q.getBindParameterCount();
  q.bind(param_count, _group);

  return q.exec();
}

// apply function on shown articles in breadth first order
int DataImpl ::MyTree ::call_on_shown_articles (
  std::function<void(Quark msg_id, Quark parent_id)> cb) const
{
  // see https://www.geeksforgeeks.org/hierarchical-data-and-how-to-query-it-in-sql/
  TimeElapsed timer;

  std::string q = R"SQL(
    with recursive hierarchy as (
      select a.id article_id, message_id, av.parent_id
      from article_view as av
	    join article as a on a.id == av.article_id
      where av.parent_id is null and status in ("e","r","s")
      union all
      select a.id, a.message_id, av.parent_id
      from article_view as av
	    join article as a on a.id == av.article_id
      join hierarchy as h
	    where status  in ("e","r","s") and av.parent_id is h.article_id
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
    cb(msg_id,prt_id);
    count++;
  }

  LOG4CXX_DEBUG(logger, "sql request done for " << count << " articles in "
                                                << timer.get_seconds_elapsed());

  return count;
}

// apply function on exposed article in breadth first order.
// Returns the number of exposed articles
int DataImpl ::MyTree ::call_on_exposed_articles (
  std::function<void(Quark msg_id, Quark parent_id)> cb) const
{
  // see https://www.geeksforgeeks.org/hierarchical-data-and-how-to-query-it-in-sql/
  std::string q = R"SQL(
    with recursive hierarchy (step, article_id, message_id, parent_id, status) as (
      select 0, a.id, message_id, av.parent_id, av.status
      from article_view as av
      join article as a on a.id == av.article_id
      where av.parent_id is null

      union all

      select step+1, a.id, a.message_id, av.parent_id, av.status
      from article_view as av
      join article as a on a.id == av.article_id
      join hierarchy as h on av.parent_id is h.article_id
      limit 10000000 -- todo remove ?
    )
    select hierarchy.message_id, parent.message_id
    from hierarchy
    left outer join article as parent on hierarchy.parent_id == parent.id
    where hierarchy.status is "e"
    order by step
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
    std::function<void(Quark msg_id, Quark new_parent_id)> cb) const {
  std::string q = R"SQL(
    select child.message_id, parent.message_id
    from article_view as av
    join article as child on av.article_id == child.id
    -- left outer join retrieved articles reparented to root
    left outer join article as parent on av.parent_id == parent.id
    where av.status is "r"
  )SQL";

  SQLite::Statement st(pan_db, q);
  int count(0);
  while (st.executeStep()) {
    std::string child_msg_id (st.getColumn(0).getText());
    std::string prt_msg_id (st.getColumn(1).getText());
    cb(child_msg_id, prt_msg_id);
    count++;
  }
  return count;
}

// apply function on hidden articles
// Returns the number of hidden articles
int DataImpl ::MyTree ::call_on_hidden_articles(
    std::function<void(Quark msg_id)> cb) const {
  std::string q = R"SQL(
    select message_id
    from article_view as av
    join article on av.article_id == article.id
    where av.status = "h"
  )SQL";

  SQLite::Statement st(pan_db, q);
  int count(0);
  while (st.executeStep()) {
    std::string msg_id (st.getColumn(0).getText());
    cb(msg_id);
    count++;
  }
  return count;
}

void DataImpl ::MyTree ::mark_as_pending_deletion(
    const std::set<const Article *> goners) const {
  std::string q = R"SQL(
    update article set to_delete = True
    where message_id == ?
  )SQL";
  SQLite::Statement st(pan_db,q);
  for (const Article *a : goners) {
    st.reset();
    st.bind(1, a->message_id);
    st.exec();
  }
}

void DataImpl ::MyTree ::update_article_view() const {
  TimeElapsed timer;
  LOG4CXX_TRACE(logger, "Update article_view");
  SQLite::Transaction setup_article_view_transaction(pan_db);

  // mark all articles
  pan_db.exec("update article_view set mark = True;");

  // found article reset the mark
  int count = fill_article_view_from_article();
  LOG4CXX_TRACE(logger, "fill article_view done ("
                            << timer.get_seconds_elapsed() << "s)");

  // set marked articles as hidden
  pan_db.exec("update article_view set status = \"h\" where mark == True;");

  // second pass to setup parent_id in article view (this needs whole
  // article_view table to compute parent_id)
  set_parent_in_article_view();
  LOG4CXX_TRACE(logger, "set_parent in article_view done ("
                            << timer.get_seconds_elapsed() << "s)");

  setup_article_view_transaction.commit();

  LOG4CXX_TRACE(logger, "Update article_view done with "
                            << count << " articles ("
                            << timer.get_seconds_elapsed() << "s).");
}

// delete (h)idden articles and set other to (s)hown
void DataImpl ::MyTree ::
    update_article_after_gui_update() const {
  LOG4CXX_DEBUG(logger, "updating article view after GUI update");
  pan_db.exec(R"SQL(
    update article_view set status = "s" where status in ("e","r");
    delete from article_view where status == "h";
    delete from article where to_delete;
  )SQL");
}

void DataImpl ::MyTree ::set_article_hidden_status(quarks_t &mids) const {
  // mark remove articles from DB
  SQLite::Statement set_hidden_status_q(pan_db, R"SQL(
    update article_view set status = "h" where
    article_id = ( select id from article where message_id == ? )
  )SQL");

  for (Quark msg_id: mids) {
    set_hidden_status_q.reset();

    set_hidden_status_q.bind(1, msg_id);
    assert(set_hidden_status_q.exec() == 1);

    LOG4CXX_TRACE(logger, "article " << msg_id << "is now hidden");
  }
};

//Article const *DataImpl ::MyTree ::get_parent(Quark const &mid) const
Article DataImpl ::MyTree ::get_parent(Quark const &mid) const
{
  SQLite::Statement q(pan_db,
                      "select message_id from article where id == (select "
                      "parent_id from article where message_id = ?) ");
  q.bind(1, mid);

  Article parent;
  while (q.executeStep())
    {
      parent.message_id = q.getColumn(0).getText();
    }
  LOG4CXX_TRACE(logger, "get_parent: " << parent.message_id << " is parent of " << mid);
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

  set_rules(rules);
  set_filter(show_type, filter);
  initialize_article_view();
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

// the quirky way of incrementing 'it' is to prevent it from being
// invalidated if update_tree() calls remove_listener()
void DataImpl ::MyTree ::fire_updates() const
{
  listeners_t::iterator it, e;
  for (it = _listeners.begin(), e = _listeners.end(); it != e;)
  {
    (*it++)->update_tree();
  }
  update_article_after_gui_update();
}

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

void DataImpl ::MyTree ::remove_articles(quarks_t const &mids)
{
  ArticleTree::Diffs diffs;
  std::set<ArticleNode *> parents;

  LOG4CXX_WARN(logger,
               "deprecated function called, removing " << mids.size()
                                                       << " articles.");
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

void DataImpl ::MyTree ::articles_changed(bool do_refilter)
{
  LOG4CXX_DEBUG(logger, "group " << _group << ": articles were changed");

  _header_rules.apply_rules(_data, _rules, _group, _save_path);

  if (do_refilter)
  {
    update_article_view();
  }

  fire_updates();

  update_article_after_gui_update();
  LOG4CXX_DEBUG(logger, "articles_changed done");
}

void DataImpl ::MyTree ::add_articles(quarks_t const &mids)
{
  LOG4CXX_DEBUG(logger, "group " << _group << ": " << mids.size() << " articles added");

  _header_rules.apply_rules(_data, _rules, _group, _save_path);

  update_article_view();
  fire_updates();
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
  LOG4CXX_WARN(logger, "deprecated add_articles called with " << nodes_in.size() << " nodes");
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
