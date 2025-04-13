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
#include <cassert>
#include <config.h>
#include <pan/data/article.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>

using namespace pan;

/****
*****  ArticleTree functions
****/

void DataImpl ::MyTree ::get_children(Quark const &mid, articles_t &setme) const
{
  if (mid.empty()) // get the roots
  {
    foreach_const (nodes_t, _nodes, it)
    {
      if (it->second->_article && ! it->second->_parent)
      {
        setme.push_back(it->second->_article);
      }
    }
  }
  else // get children of a particular article
  {
    nodes_t::const_iterator parent_it(_nodes.find(mid));
    if (parent_it != _nodes.end())
    {
      ArticleNode::children_t &kids(parent_it->second->_children);
      setme.reserve(kids.size());
      foreach_const (ArticleNode::children_t, kids, it)
      {
        setme.push_back((*it)->_article);
      }
    }
  }
}

Article const *DataImpl ::MyTree ::get_parent(Quark const &mid) const
{
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
  nodes_t::const_iterator it(_nodes.find(mid));
  return it == _nodes.end() ? nullptr : it->second->_article;
}

size_t DataImpl ::MyTree ::size() const
{
  return _nodes.size();
}

void DataImpl ::MyTree ::set_rules(RulesInfo const *rules)
{

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

  GroupHeaders const *h(_data.get_group_headers(_group));
  g_assert(h != nullptr);
  const_nodes_v candidates;
  candidates.reserve(h->_nodes.size());
  foreach_const (nodes_t, h->_nodes, it)
  {
    ArticleNode const *node(it->second);
    if (node->_article)
    {
      candidates.push_back((ArticleNode *)node);
    }
  }

  apply_rules(candidates);
}

void DataImpl ::MyTree ::set_filter(Data::ShowType const show_type,
                                    FilterInfo const *criteria)
{
  // set the filter...
  if (criteria)
  {
    _filter = *criteria;
  }
  else
  {
    _filter.clear();
  }
  _show_type = show_type;

  // refilter all the articles in the group...
  GroupHeaders const *h(_data.get_group_headers(_group));
  g_assert(h != nullptr);
  const_nodes_v candidates;
  candidates.reserve(h->_nodes.size());
  foreach_const (nodes_t, h->_nodes, it)
  {
    ArticleNode const *node(it->second);
    if (node->_article)
    {
      candidates.push_back(node);
    }
  }

  apply_filter(candidates);
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

  _data.ref_group(_group);
  _data._trees.insert(this);

  set_filter(show_type, filter);
  set_rules(rules);
}

DataImpl ::MyTree ::~MyTree()
{
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

void DataImpl ::MyTree ::apply_rules(const_nodes_v &candidates)
{
  //  std::cerr<<"apply rules mytree\n";

  // apply the rules to the whole tree.
  foreach (const_nodes_v, candidates, it)
  {
    if (! (*it)->_article)
    {
      continue;
    }
    _data._article_rules.apply_rules(_data, _rules, _group, *(*it)->_article);
  }

  // now act on result of applied rules
  cache_articles(_data._article_rules._cached);
  download_articles(_data._article_rules._downloaded);
  _data._article_rules.finalize(_data);
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

// candidates holds GroupHeader's ArticleNodes pointers
// candidates are sorted by Mid (see NodeMidCompare)
void DataImpl ::MyTree ::apply_filter(const_nodes_v const &candidates)
{
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
                 _data, _filter, _group, *(*it)->_article))
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
    const_nodes_v passcopy = pass;
    foreach (const_nodes_v, passcopy, it)
    {
      ArticleNode const *n(*it);
      while ((n = n->_parent))
      {
        pass.push_back(n);
      }
    }
    std::sort(pass.begin(), pass.end(), compare);
    pass.erase(std::unique(pass.begin(), pass.end()), pass.end());
    // std::cerr << LINE_ID << " reduces to " << pass.size() << " threads\n";
  }

  if (_show_type == Data::SHOW_THREADS || _show_type == Data::SHOW_SUBTHREADS)
  {
    unique_nodes_t d;
    foreach_const (const_nodes_v, pass, it)
    {
      accumulate_descendants(d, *it);
    }
    // std::cerr << LINE_ID << " expands into " << d.size() << " articles\n";

    const_nodes_v fail2;
    pass.clear();
    foreach_const (unique_nodes_t, d, it)
    {
      Article const *a((*it)->_article);
      if (a->score > -9999
          || _data._article_filter.test_article(_data, _filter, _group, *a))
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

void DataImpl ::MyTree ::accumulate_descendants(unique_nodes_t &descendants,
                                                ArticleNode const *node) const
{
  // if this node has an article and wasn't already in `descendants',
  // then add it and its children.

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
  //  std::cerr<<"articles changed\n";

  const_nodes_v nodes;
  _data.find_nodes(mids, _data.get_group_headers(_group)->_nodes, nodes);
  apply_rules(nodes);

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
  //  std::cerr<<"add articles\n";

  const_nodes_v nodes;
  _data.find_nodes(mids, _data.get_group_headers(_group)->_nodes, nodes);
  apply_rules(nodes);
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
