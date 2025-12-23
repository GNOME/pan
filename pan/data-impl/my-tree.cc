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

#include <fmt/format.h>
#include "data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/data/pan-db.h"
#include "pan/general/log4cxx.h"
#include "pan/general/time-elapsed.h"
#include <SQLiteCpp/Statement.h>
#include <cassert>
#include <config.h>
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
  TimeElapsed timer;
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
                << " rows" << " in " << timer.get_seconds_elapsed() << "s");
}

void DataImpl ::MyTree ::initialize_article_view() const
{
  TimeElapsed timer;
  LOG4CXX_TRACE(logger, "Initial load on article_view table");
  SQLite::Transaction setup_article_view_transaction(pan_db);

  reset_article_view();

  int count = fill_article_view_from_article();
  auto t = timer.get_seconds_elapsed();
  LOG4CXX_TRACE(
    logger, "init: fill article_view done (" << t << "s)");


  // second pass to setup parent_id in article view (this needs whole
  // article_view table to compute parent_id)
  set_parent_in_article_view();
  auto t2 = timer.get_seconds_elapsed();
  LOG4CXX_TRACE(
    logger, "init: set parent in article_view done (" << t2 - t << "s)");

  setup_article_view_transaction.commit();

  auto t3 = timer.get_seconds_elapsed();
  LOG4CXX_TRACE(logger, "Initial load on article_view table done with "
                            << count << " articles (commit " << t3 - t2
                            << "s, total " << timer.get_seconds_elapsed()
                            << "s)");
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
       with f (article_id) as (
         select article.id
         from article
         join article_group as ag on ag.article_id = article.id
         join `group` as g on ag.group_id = g.id
    )SQL" +
         join + "where " + (where.empty() ? "true" : where) + R"SQL(
         and g.name == ? and not article.to_delete
       )
       insert into article_view (article_id, status)
       select distinct article_id, "e" from f where true
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

void DataImpl::MyTree::set_join_and_column(header_column_enum &header_column_id,
                                 std::string &db_column,
                                 std::string &db_join) const {
  switch (header_column_id) {
  case COL_STATE:
    db_column = "a.part_state";
    break;
  case COL_SUBJECT:
    db_column = "subject.subject";
    db_join = "join subject on subject.id == a.subject_id";
    break;
  case COL_SCORE:
    db_column = "ag.score";
    db_join = "join article_group as ag on ag.article_id == a.id";
    break;
  case COL_SHORT_AUTHOR:
    db_column = "author.author";
    db_join = "join author on a.author_id == author.id";
    break;
  case COL_LINES:
    db_column = "a.line_count";
    break;
  case COL_BYTES:
    db_column = "a.bytes";
    break;
  case COL_DATE:
  case COL_DATE_STR:
    db_column = "time_posted";
    break;
  default:
    LOG4CXX_FATAL(logger, "Internal error: unexpected column id: " << header_column_id);
    assert(0); // Unknown column
  }
}

int run_sql(std::string &q,
            std::vector<Data::ArticleTree::Thread> &threads) {
  TimeElapsed timer;

  threads.clear(); // safety measure

  LOG4CXX_TRACE(logger, "sql request is " << q);

  SQLite::Statement st(pan_db, q);
  int count(0);
  Quark current_prt_id, prt_id;
  Data::ArticleTree::Thread root, thread;
  Data::ArticleTree::Thread::Child child;
  std::map<Quark, bool> checkPrt;

  root.parent_id = current_prt_id;
  root.status = pan::Data::ArticleTree::ThreadStatus::Unknown;
  threads.push_back(root);

  checkPrt[current_prt_id] = true;

  while (st.executeStep()) {
    child.msg_id = st.getColumn(0).getText();
    child.set_status(st.getColumn(1));
    child.sort_index = st.getColumn(2);
    if (st.getColumn(3).isNull()) {
      prt_id.clear();
    }
    else {
      prt_id = st.getColumn(3).getText();
    }
    if (prt_id != current_prt_id) {
      thread.parent_id = prt_id;
      thread.set_status(st.getColumn(4));
      current_prt_id = prt_id;
      assert(!checkPrt[current_prt_id]);
      threads.push_back(thread);
      thread.children.clear();
    }
    threads.back().children.push_back(child);
    count++;
  }

  LOG4CXX_DEBUG(logger, "sql request done for " << count << " articles in "
                                                << timer.get_seconds_elapsed());

  return count;
}

int DataImpl ::MyTree ::get_shown_threads(
  std::vector<Data::ArticleTree::Thread> &threads,
  header_column_enum sort_column, bool sort_ascending) const {
  return get_threads(threads, sort_column, sort_ascending, R"-(in ("e","r","s"))-");
}

// apply function on shown articles. In theory, article thread result
// from exchange between servers, a parent article always have a time
// stamp lower than its children, so ordering articles by time stamp
// is enough to get parents before children. In practice, this does
// not work, so we must recursively scan the article to find the
// parents first.
int DataImpl ::MyTree ::get_threads(
  std::vector<Data::ArticleTree::Thread> &threads,
  header_column_enum sort_column, bool sort_ascending, std::string status_cond) const {

  // Map column IDs to database column names
  std::string db_column, db_join;
  set_join_and_column(sort_column, db_column, db_join);

  std::string direction(sort_ascending ? "asc" : "desc");
  // See
  // https://www.geeksforgeeks.org/hierarchical-data-and-how-to-query-it-in-sql/
  // see https://sqlite.org/windowfunctions.html for row_number function
  std::string format = R"SQL(
    with recursive hierarchy (step, a_id, m_id, p_id, h_time, status, prt_status, sort_data) as (
      -- all non-filtered not hidden root articles
      select 1, a.id, message_id, av.parent_id, a.time_posted, av.status, null, {0}
      from article_view as av
      join article as a on a.id == av.article_id
      {1}
      where av.parent_id is null

      union all

      -- all non-filtered not hidden children of a parent article
      select step+1, a.id, a.message_id, av.parent_id, a.time_posted, av.status, h.status, {0}
      from article_view as av
      join article as a on a.id == av.article_id
      {1}
      join hierarchy as h
	    where av.parent_id is not null and av.parent_id = h.a_id
      limit 10000000 -- todo remove ?
    ),
    -- use another CTE because window used to add sort_index cannot be used in recursive CTE
    all_visible_article (step, msg_id, sort_index, status, prt_msg_id, prt_status) as (
      -- inject row_number and parent_message_id
      select step, hierarchy.m_id, row_number() over thread_window, status,
        (select message_id from article as a where a.id = hierarchy.p_id) as prt_msg_id,
        prt_status
      from hierarchy
      window thread_window as (
        partition by hierarchy.p_id
        -- article within threads are always sorted by ascending date
        order by case step when 1 then sort_data end collate nocase {3}, h_time asc
      )
    )
    -- now we can order by required status
    select msg_id, status, sort_index, prt_msg_id, prt_status from all_visible_article
    {2}
    order by step, prt_msg_id, sort_index
  )SQL";

  std::string where = status_cond.empty() ? "" : "where status " + status_cond ;
  std::string q = fmt::format(format,db_column, db_join, where, direction);

  return run_sql(q, threads);
}

// apply function on shown articles in breadth first order This is
// similar to get_shown_threads, except that this function is called
// when getting articles in ordered threads is not necessary. Root and
// parent threads are already created, just their order needs to
// change. So there's no need to create a complex SQL query that
// returns ordered threads.
int DataImpl ::MyTree ::get_sorted_shown_threads(
  std::vector<Data::ArticleTree::Thread> &threads,
    header_column_enum header_column_id, bool ascending) const {

  // Map column IDs to database column names
  std::string db_column, db_join;
  std::string direction(ascending ? "asc" : "desc");

  set_join_and_column(header_column_id, db_column, db_join);

  // prt_msg_id is not allowed in window definition, must use a CTE
  std::string format = R"SQL(
    with thread (msg_id, status, time_posted, sort_data, prt_msg_id, prt_status) as (
      select message_id, status, time_posted, {0},
            (select message_id from article as a_in where a_in.id = av.parent_id) as prt_msg_id,
            (select status from article_view as av_in where av_in.article_id = av.parent_id) as prt_status
      from article_view as av
      join article as a on a.id == av.article_id
      {1}
      where status is not "h"
    )
    select msg_id, status, row_number() over thread_window, prt_msg_id, prt_status
    from thread
    window thread_window as (
      partition by prt_msg_id
      -- articles within thread are always sorted by ascending date
      order by prt_msg_id,
        case when prt_msg_id is null then sort_data end {2},
             time_posted asc
    )
    order by prt_msg_id,
      case when prt_msg_id is null then sort_data end {2},
           time_posted asc
  )SQL";

  std::string q = fmt::format(format, db_column, db_join, direction);

  return run_sql(q, threads);
}

void DataImpl ::MyTree ::get_shown_parent_ids(
    std::vector<Quark> &shown_parents_ids
  ) const {
  TimeElapsed timer;
  std::string q = R"SQL(
    select distinct article.message_id as prt_msg_id from article_view
    join article on article.id == article_view.parent_id
    where status is not "h"
  )SQL";
  SQLite::Statement st(pan_db, q);

  while (st.executeStep()) {
    Quark prt_id = st.getColumn(0).getText();
    shown_parents_ids.push_back(prt_id);
  }

  LOG4CXX_DEBUG(logger, "got " << shown_parents_ids.size() << " parents in "
                                                << timer.get_seconds_elapsed());
}

// apply function on exposed article in breadth first order.
// Returns the number of exposed articles
int DataImpl ::MyTree ::get_exposed_articles (
          std::vector<Data::ArticleTree::Thread> &threads,
          header_column_enum header_column_id,
          bool ascending) const {
  return get_threads(threads, header_column_id, ascending, "is \"e\"");
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
int DataImpl ::MyTree ::get_hidden_articles(quarks_t &fillme) const {
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
    fillme.insert(msg_id);
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
  LOG4CXX_TRACE(logger, "start");
  SQLite::Transaction setup_article_view_transaction(pan_db);

  // mark all articles
  pan_db.exec("update article_view set mark = True;");
  auto tmp_time = timer.get_seconds_elapsed();
  LOG4CXX_TRACE(logger, "set mark in article_view done ("
                            << timer.get_seconds_elapsed() - tmp_time << "s)");
  tmp_time = timer.get_seconds_elapsed();

  // found article reset the mark
  int count = fill_article_view_from_article();
  LOG4CXX_TRACE(logger, "fill article_view done ("
                            << timer.get_seconds_elapsed() - tmp_time << "s)");
  tmp_time = timer.get_seconds_elapsed();

  // set marked articles as hidden
  pan_db.exec("update article_view set status = \"h\" where mark == True;");

  // second pass to setup parent_id in article view (this needs whole
  // article_view table to compute parent_id)
  set_parent_in_article_view();
  LOG4CXX_TRACE(logger, "set_parent in article_view done ("
                            << timer.get_seconds_elapsed() - tmp_time << "s)");
  tmp_time = timer.get_seconds_elapsed();

  setup_article_view_transaction.commit();

  LOG4CXX_TRACE(logger, "transaction commit done in "
                            << timer.get_seconds_elapsed() - tmp_time << "s).");

  LOG4CXX_TRACE(logger, "done with " << count << " articles (total time: "
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

bool DataImpl ::MyTree ::has_article() const {
  // see https://stackoverflow.com/a/9756276 for select exists trick
  SQLite::Statement q(pan_db, R"SQL(
     select exists(
       select 1 from article_group as ag
         join `group` as g on ag.group_id = g.id
         where g.name == ?
     )
  )SQL");
  q.bind(1,_group);

  bool result(false);
  while (q.executeStep())
    {
      result = q.getColumn(0).getInt();
    }
  LOG4CXX_TRACE(logger, "has_article: group " << _group << " result is " << result);
  return result;
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
  _data._tree = this;

  set_rules(rules);
  set_filter(show_type, filter);
}

DataImpl ::MyTree ::~MyTree()
{
  LOG4CXX_DEBUG(logger, "Destroying MyTree of group " << _group);
  _data._tree = nullptr;
  _data.unref_group(_group);
}

// the quirky way of incrementing 'it' is to prevent it from being
// invalidated if update_tree() calls remove_listener()
void DataImpl ::MyTree ::fire_updates() const
{
  listeners_t::iterator it, e;
  for (it = _listeners.begin(), e = _listeners.end(); it != e;)
  {
    (*it++)->update_gui_tree();
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

