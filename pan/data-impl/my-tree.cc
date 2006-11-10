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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <cassert>
#include <glib/gmessages.h> // for g_assert
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
#include <pan/general/quark.h>
#include <pan/data/article.h>
#include <pan/data/filter-info.h>
#include "article-filter.h"
#include "data-impl.h"

using namespace pan;

/****
*****  ArticleTree functions
****/

void
DataImpl :: MyTree :: get_children (const Quark& mid, articles_t & setme) const
{
  if (mid.empty()) // get the roots
  {
    foreach_const (nodes_t, _nodes, it)
    if (it->second->_article && !it->second->_parent)
      setme.push_back (it->second->_article);
  }
  else // get children of a particular article
  {
    nodes_t::const_iterator parent_it (_nodes.find (mid));
    if (parent_it != _nodes.end()) {
      ArticleNode::children_t& kids (parent_it->second->_children);
      setme.reserve (kids.size());
      foreach_const (ArticleNode::children_t, kids, it)
        setme.push_back ((*it)->_article);
    }
  }
}

const Article*
DataImpl :: MyTree :: get_parent (const Quark& mid) const
{
  const Article * parent (0);
  const ArticleNode * parent_node (0);

  nodes_t::const_iterator child_it (_nodes.find (mid));
  if (child_it != _nodes.end())
    parent_node = child_it->second->_parent;
  if (parent_node)
    parent = parent_node->_article;

  return parent;
}

const Article*
DataImpl :: MyTree :: get_article (const Quark& mid) const
{
  nodes_t::const_iterator it (_nodes.find (mid));
  return it==_nodes.end() ? 0 : it->second->_article;
}

size_t
DataImpl :: MyTree :: size () const 
{
  return _nodes.size();
}

void
DataImpl :: MyTree :: set_filter (const Data::ShowType    show_type,
                                  const FilterInfo      * criteria)
{
  // set the filter...
  if (criteria)
    _filter = *criteria;
  else 
    _filter.clear ();
  _show_type = show_type;

  // refilter all the articles in the group...
  const GroupHeaders * h (_data.get_group_headers (_group));
  g_assert (h != 0);
  const_nodes_v candidates;
  candidates.reserve (h->_nodes.size());
  foreach_const (nodes_t, h->_nodes, it) {
    const ArticleNode * node (it->second);
    if (node->_article)
      candidates.push_back (node);
  }

  apply_filter (candidates);
}

/****
*****  Life Cycle
****/

DataImpl :: MyTree :: MyTree (DataImpl              & data_impl,
                              const Quark           & group,
                              const Data::ShowType    show_type,
                              const FilterInfo      * filter):
  _group (group),
  _data (data_impl)
{
  _data.ref_group (_group);
  _data._trees.insert (this);
  set_filter (show_type, filter);
}

DataImpl :: MyTree :: ~MyTree ()
{
  _nodes.clear ();
  _data._trees.erase (this);
  _data.unref_group (_group);
}

/****
*****  
****/

struct
DataImpl :: MyTree :: NodeMidCompare
{
  typedef std::pair<const pan::Quark, pan::DataImpl::ArticleNode*> nodes_v_pair;

  bool operator () (const ArticleNode* a, const nodes_v_pair& b) const
    { return a->_mid < b.first; }
  bool operator () (const nodes_v_pair& a, const ArticleNode* b) const
    { return a.first < b->_mid; }

  bool operator () (const ArticleNode* a, const ArticleNode* b) const
    { return a->_mid < b->_mid; }

  bool operator () (const Quark& a, const ArticleNode* b) const
    { return a < b->_mid; }
  bool operator () (const ArticleNode* a, const Quark& b) const
    { return a->_mid < b; }
};


// candidates holds GroupHeader's ArticleNodes pointers
// candidates are sorted by Mid (see NodeMidCompare)
void
DataImpl :: MyTree :: apply_filter (const const_nodes_v& candidates)
{
  NodeMidCompare compare;

  const_nodes_v pass;
  const_nodes_v fail;
  pass.reserve (candidates.size());
  fail.reserve (candidates.size());

  // apply the filter to the whole tree
  if (_filter.empty())
    pass = candidates;
  else foreach_const (const_nodes_v, candidates, it) {
    if (!(*it)->_article)
      continue;
    else if (_data._article_filter.test_article (_data, _filter, _group, *(*it)->_article))
      pass.push_back (*it);
    else
      fail.push_back (*it);
  }
  //std::cerr << LINE_ID << " " << pass.size() << " of "
  //          << (pass.size() + fail.size()) << " articles pass\n";

  //  maybe include threads or subthreads...
  if (_show_type == Data::SHOW_THREADS)
  {
    foreach (const_nodes_v, pass, it) {
      const ArticleNode *& n (*it);
      while (n->_parent)
        n = n->_parent;
    }
    std::sort (pass.begin(), pass.end(), compare);
    pass.erase (std::unique (pass.begin(), pass.end()), pass.end());
    //std::cerr << LINE_ID << " reduces to " << pass.size() << " threads\n";
  }

  if (_show_type == Data::SHOW_THREADS || _show_type == Data::SHOW_SUBTHREADS)
  {
    unique_nodes_t d;
    foreach_const (const_nodes_v, pass, it)
      accumulate_descendants (d, *it);
    //std::cerr << LINE_ID << " expands into " << d.size() << " articles\n";

    pass.clear ();
    foreach_const (unique_nodes_t, d, it) {
      const Article * a ((*it)->_article);
      if (a->score > -9999 || _data._article_filter.test_article (_data, _filter, _group, *a))
        pass.push_back (*it); // pass is now sorted by mid because d was too
      else
        fail.push_back (*it); // fail is now unsorted and may have duplicates
    }

    // fail cleanup: remove duplicates
    std::sort (fail.begin(), fail.end(), compare);
    fail.erase (std::unique (fail.begin(), fail.end()), fail.end());

    // fail cleanup: remove newly-passing articles
    const_nodes_v tmp;
    tmp.reserve (fail.size());
    std::set_difference (fail.begin(), fail.end(),
                         pass.begin(), pass.end(),
                         inserter (tmp, tmp.begin()),
                         compare);
    fail.swap (tmp);
    //std::cerr << LINE_ID << ' ' << pass.size() << " of "
    //          << (pass.size() + fail.size()) 
    //          << " make it past the show-thread block\n";
  }


  // passing articles not in the tree should be added...
  add_articles (pass);

  // failing articles in the tree should be removed...
  quarks_t mids;
  foreach_const (const_nodes_v, fail, it)
    mids.insert (mids.end(), (*it)->_mid);
  remove_articles (mids);
}

void
DataImpl :: MyTree :: remove_articles (const quarks_t& mids)
{
  ArticleTree::Diffs diffs;
  std::set<ArticleNode*> parents;

  // zero out any corresponding nodes in the tree...
  nodes_v nodes;
  _data.find_nodes (mids, _nodes, nodes);
  foreach_const (nodes_v, nodes, it)
  {
    ArticleNode * node (*it);
    const Quark& mid (node->_mid);

    if (node->_article) {
        node->_article = 0;
        diffs.removed.insert (diffs.removed.end(), mid);
    }

    if (node->_parent)
      parents.insert (node->_parent);
  }

  foreach (std::set<ArticleNode*>, parents, it)
  {
    ArticleNode *parent (*it);
    ArticleNode::children_t kids;
    foreach (ArticleNode::children_t, parent->_children, kit)
      if (!mids.count ((*kit)->_mid))
        kids.push_back (*kit);
    parent->_children.swap (kids);
  }

  // reparent any children who need it...
  foreach_const (nodes_t, _nodes, it)
  {
    ArticleNode * n (it->second);

    // if it's an article with a removed parent, reparent to ancestor
    if (n->_article && n->_parent && !n->_parent->_article)
    {
      // start filling out a diff entry
      ArticleTree::Diffs::Reparent r;
      r.message_id = n->_mid;
      r.old_parent = n->_parent->_mid;

      // look for a suitable parent
      ArticleNode * newparent (n->_parent);
      while (newparent && !newparent->_article)
        newparent = newparent->_parent;

      // reparent the node
      if (newparent) {
        newparent->_children.push_front (n);
        r.new_parent = newparent->_mid;
      }
      n->_parent = newparent;

      diffs.reparented.push_back (r);
    }
  }

  fire_diffs (diffs);

  foreach_const (quarks_t, diffs.removed, it)
    _nodes.erase (*it);
}

void
DataImpl :: MyTree :: accumulate_descendants (unique_nodes_t& descendants,
                                              const ArticleNode* node) const
{
  // if this node has an article and wasn't already in `descendants',
  // then add it and its children.

  if (node->_article && descendants.insert(node).second)
    foreach_const (ArticleNode::children_t, node->_children, it)
      accumulate_descendants (descendants, *it);
}

void
DataImpl :: MyTree :: articles_changed (const quarks_t& mids, bool do_refilter)
{
  if (do_refilter) {
    // refilter... this may cause articles to be shown or hidden
    const_nodes_v nodes;
    _data.find_nodes (mids, _data.get_group_headers(_group)->_nodes, nodes);
    apply_filter (nodes);
  }

  // fire an update event for any of those mids in our tree...
  nodes_v my_nodes;
  _data.find_nodes (mids, _nodes, my_nodes);
  if (!my_nodes.empty()) {
    ArticleTree::Diffs diffs;
    foreach_const (nodes_v, my_nodes, it)
      diffs.changed.insert (diffs.changed.end(), (*it)->_mid);
    fire_diffs (diffs);
  }
}

void
DataImpl :: MyTree :: add_articles (const quarks_t& mids)
{
  const_nodes_v nodes;
  _data.find_nodes (mids, _data.get_group_headers(_group)->_nodes, nodes);
  apply_filter (nodes);
}


struct
DataImpl :: MyTree :: TwoNodes
{
  const ArticleNode * node;
  ArticleNode * tree_node;
  TwoNodes (const ArticleNode *n, ArticleNode *t): node(n), tree_node(t) {}
};

void
DataImpl :: MyTree :: add_articles (const const_nodes_v& nodes_in)
{
  NodeMidCompare compare;

  ///
  ///  1. add the new articles
  ///

  // sort the nodes by Message-Id
  // so that we can use `nodes' for set operations
  const_nodes_v nodes (nodes_in);
  std::sort (nodes.begin(), nodes.end(), compare);

  // nodes -= this->_nodes;
  // (don't try to add articles we've already got.)
  if (1) {
    const_nodes_v tmp;
    tmp.reserve (nodes.size());
    std::set_difference (nodes.begin(), nodes.end(),
                         _nodes.begin(), _nodes.end(),
                         inserter (tmp, tmp.begin()),
                         compare);
    nodes.swap (tmp);
    //std::cerr << LINE_ID << ' ' << nodes.size() << " unique nodes\n";
  }

  // build new MyTree nodes for each of the articles being added
  int node_index (_node_chunk.size());
  _node_chunk.resize (_node_chunk.size() + nodes.size());
  nodes_v tree_nodes;
  tree_nodes.reserve (nodes.size());
  foreach_const (const_nodes_v, nodes, it) {
    const Article * a ((*it)->_article);
    ArticleNode * node (&_node_chunk[node_index++]);
    node->_mid = a->message_id;
    node->_article = const_cast<Article*>(a);
    //std::cerr << LINE_ID << " added " << node->_mid << " to the tree\n";
    std::pair<nodes_t::iterator,bool> result (
      _nodes.insert (std::pair<Quark,ArticleNode*>(node->_mid, node)));
    g_assert (result.second); // freshly added; not a duplicate
    tree_nodes.push_back (node);
  }

  ///
  ///  2. find parents for the new articles
  ///

  ArticleTree::Diffs diffs;
  for (size_t i(0), n(nodes.size()); i!=n; ++i)
  {
    const ArticleNode * node (nodes[i]);
    ArticleNode * tree_node (tree_nodes[i]);
    g_assert (node->_mid == tree_node->_mid);

    Diffs::Added added;
    added.message_id = tree_node->_mid;

    // find the first ancestor that's present in our tree
    ArticleNode * parent (0);
    const nodes_t::const_iterator nend (_nodes.end());
    for (const ArticleNode *it(node->_parent); it && !parent; it=it->_parent) {
      nodes_t::iterator nit (_nodes.find (it->_mid));
      if (nit != nend)
        parent = nit->second;
    }

    // if we found a parent, use it.
    if (parent) {
      tree_node->_parent = parent;
      parent->_children.push_back (tree_node);
      added.parent = parent->_mid;
    }

    diffs.added.push_back (added);
    //std::cerr << LINE_ID << " child " << added.message_id
    //                     << " has parent " << added.parent << std::endl;
  }

  ///
  ///  3. possibly reparent other articles
  ///

  // get a list of all articles that are descendants of the new articles
  const_nodes_v descendants;
  if (1) {
    unique_nodes_t tmp;
    foreach (const_nodes_v, nodes, it)
      accumulate_descendants (tmp, *it);
    descendants.assign (tmp.begin(), tmp.end());
  }

  // descendants = descendants - nodes;
  // (we parented `nodes' in step 2.)
  if (1) {
    const_nodes_v tmp;
    std::set_difference (descendants.begin(), descendants.end(),
                         nodes.begin(), nodes.end(),
                         inserter (tmp, tmp.begin()));
    descendants.swap (tmp);
  }

  // map the 'canonical' nodes to MyTree nodes...
  typedef std::vector<TwoNodes> twonodes_v;
  twonodes_v descend;
  descend.reserve (descendants.size());
  if (1) {
    nodes_t::iterator nit(_nodes.begin()), nend(_nodes.end());
    const_nodes_v::const_iterator dit(descendants.begin()),
                                 dend(descendants.end());
    while (nit!=nend && dit!=dend) {
      if (nit->second->_mid < (*dit)->_mid)
        ++nit;
      else if ((*dit)->_mid < nit->second->_mid)
        ++dit;
      else {
        g_assert (nit->second->_mid == (*dit)->_mid);
        descend.push_back (TwoNodes (*dit, nit->second));
        ++nit;
        ++dit;
      }
    }
  }

  // now walk though the descendants and possibly reparent them.
  foreach_const (twonodes_v, descend, it)
  {
    ArticleNode * tree_node (it->tree_node);
    const ArticleNode * node (it->node);
    g_assert (node->_mid == tree_node->_mid);

    //std::cerr << LINE_ID << " looking for a new parent for "
    //          << tree_node->_mid << std::endl;
    ArticleNode * new_parent (0);
    node = node->_parent;
    while (node && !new_parent) {
      //std::cerr << LINE_ID << " maybe " << node->_mid
      //                     << " can be its new parent..." << std::endl;
      nodes_t::iterator nit (_nodes.find (node->_mid));
      if (nit != _nodes.end())
        new_parent = nit->second;
      else {
        node = node->_parent;
        //std::cerr << LINE_ID << " but no, that's not in the tree.\n";
      }
    }
    //std::cerr << LINE_ID << " " << tree_node->_mid << "'s best parent is "
    //          << new_parent << std::endl;

    if (new_parent == tree_node->_parent)
      continue;

    ArticleNode * old_parent (tree_node->_parent);
    ArticleTree::Diffs::Reparent reparent;
    if (old_parent) {
      reparent.old_parent = old_parent->_mid;
      old_parent->_children.remove (tree_node);
    }
    new_parent->_children.push_back (tree_node);
    tree_node->_parent = new_parent;
    reparent.message_id = tree_node->_mid;
    reparent.new_parent = new_parent->_mid;
    diffs.reparented.push_back (reparent);
    //std::cerr << LINE_ID << " REPARENTED: " << reparent.message_id
    //          << " has a new parent " << reparent.new_parent << std::endl;
  }

  // std::cerr << LINE_ID << ' ' << _nodes.size() << " articles in the tree\n";
  if (!diffs.reparented.empty() || !diffs.added.empty())
    fire_diffs (diffs);
}
