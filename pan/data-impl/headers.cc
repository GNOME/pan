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

#include "pan/data/data.h"
#include "pan/general/string-view.h"
#include "pan/general/article_number.h"
#include "pan/usenet-utils/numbers.h"
#include <SQLiteCpp/Statement.h>
#include <cerrno>
#include <cmath>
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <glib.h>
#include <glib/gi18n.h>
#include <log4cxx/logger.h>
#include <map>
#include <ostream>
#include <string>
#include <vector>
extern "C"
{
#include <sys/stat.h>  // for chmod
#include <sys/types.h> // for chmod
}
#include "article-filter.h"
#include "data-impl.h"
#include <pan/data/article.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/log4cxx.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/quark.h>
#include <pan/general/time-elapsed.h>
#include <pan/usenet-utils/filter-info.h>

using namespace pan;

namespace {
log4cxx::LoggerPtr logger = pan::getLogger("header");
log4cxx::LoggerPtr tree_logger = pan::getLogger("header-tree");
}

DataImpl ::GroupHeaders ::GroupHeaders() :

  _ref(0),
  _dirty(false)
{
}

DataImpl ::GroupHeaders ::~GroupHeaders()
{
}

DataImpl ::ArticleNode *DataImpl ::GroupHeaders ::find_node(Quark const &mid)
{
  ArticleNode *node(nullptr);
  nodes_t::iterator it(_nodes.find(mid));
  if (it != _nodes.end())
  {
    node = it->second;
  }
  return node;
}

DataImpl ::ArticleNode const *DataImpl ::GroupHeaders ::find_node(
  Quark const &mid) const
{
  ArticleNode const *node(nullptr);
  nodes_t::const_iterator it(_nodes.find(mid));
  if (it != _nodes.end())
  {
    node = it->second;
  }
  return node;
}

void DataImpl ::GroupHeaders ::remove_articles(quarks_t const &mids)
{
  nodes_v nodes;
  find_nodes(mids, _nodes, nodes);
  foreach (nodes_v, nodes, it)
  {
    (*it)->_article = nullptr;
  }
  _dirty = true;
}

DataImpl ::GroupHeaders const *DataImpl ::get_group_headers(
  Quark const &group) const
{
  group_to_headers_t::const_iterator it(_group_to_headers.find(group));
  return it == _group_to_headers.end() ? nullptr : it->second;
}

DataImpl ::GroupHeaders *DataImpl ::get_group_headers(Quark const &group)
{
  group_to_headers_t::iterator it(_group_to_headers.find(group));
  return it == _group_to_headers.end() ? nullptr : it->second;
}

void DataImpl::GroupHeaders::reserve(Article_Count articles)
{
  _art_chunk.reserve(static_cast<Article_Count::type>(articles));
  // A note - the number of nodes is very rarely the same as the number of
  // articles, but it is generally not far out, so at worse you'll end up with
  // two allocations
  _node_chunk.reserve(static_cast<Article_Count::type>(articles));
}

void DataImpl ::get_article_references(Article const *article,
                                       std::string &setme) const
{
  SQLite::Statement ref_q(pan_db, R"SQL(
    select `references` from article where message_id = ?
  )SQL" );

  ref_q.bind(1, article->message_id);

  while (ref_q.executeStep())
  {
    setme.assign(ref_q.getColumn(0).getText());
  }
}

void DataImpl ::free_group_headers_memory(Quark const &group)
{
  group_to_headers_t::iterator it(_group_to_headers.find(group));
  if (it != _group_to_headers.end())
  {
    delete it->second;
    _group_to_headers.erase(it);
  }
}

void DataImpl ::ref_group(Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  SQLite::Statement read_article_xref_q(pan_db,R"SQL(
    select migrated from `group` where name = ? ;
  )SQL");
  read_article_xref_q.bind(1, group);

  if (! h)
  {
    h = _group_to_headers[group] = new GroupHeaders();
    bool migrated;
    int count(0);
    while (read_article_xref_q.executeStep()) {
      migrated = read_article_xref_q.getColumn(0).getInt() == 1;
      count++;
    }
    assert(count == 1);

    if (!migrated) {
      migrate_headers(*_data_io, group);
      update_part_states(group);
      migrate_read_ranges(group);

      SQLite::Statement set_q(pan_db, "update `group` set migrated = True where name = ?");
      set_q.bind(1, group.c_str());
      int res = set_q.exec();
      assert(res == 1);
    }
    load_headers_from_db(group);
  }
  ++h->_ref;
  //  std::cerr << LINE_ID << " group " << group << " refcount up to " <<
  //  h->_ref << std::endl;
}

void DataImpl ::unref_group(Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  pan_return_if_fail(h != nullptr);

  --h->_ref;
  //  std::cerr << LINE_ID << " group " << group << " refcount down to " <<
  //  h->_ref << std::endl;
  if (h->_ref == 0)
  {
    //    if (h->_dirty )
    h->_dirty = false;
    free_group_headers_memory(group);
  }
}

void DataImpl ::fire_article_flag_changed(articles_t &a, Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  h->_dirty = true;
  Data::fire_article_flag_changed(a, group);
}

void DataImpl ::find_nodes(quarks_t const &mids, nodes_t &nodes, nodes_v &setme)
{
  NodeWeakOrdering o;
  nodes_t tmp;
  std::set_intersection(nodes.begin(),
                        nodes.end(),
                        mids.begin(),
                        mids.end(),
                        std::inserter(tmp, tmp.begin()),
                        o);

  setme.reserve(tmp.size());
  foreach_const (nodes_t, tmp, it)
  {
    setme.push_back(it->second);
  }
}

void DataImpl ::find_nodes(quarks_t const &mids,
                           nodes_t const &nodes,
                           const_nodes_v &setme)
{
  NodeWeakOrdering o;
  nodes_t tmp;
  std::set_intersection(nodes.begin(),
                        nodes.end(),
                        mids.begin(),
                        mids.end(),
                        std::inserter(tmp, tmp.begin()),
                        o);

  setme.reserve(tmp.size());
  foreach_const (nodes_t, tmp, it)
  {
    setme.push_back(it->second);
  }
}

void DataImpl ::insert_part_in_db(Quark const &group,
                          Quark const &article_mid,
                          int number,
                          StringView const &part_mid,
                          unsigned long lines,
                          unsigned long bytes)
{
  SQLite::Statement insert_q(pan_db,R"SQL(
    insert into `article_part` (article_id, part_number, part_message_id, size)
    values ((select id from article where message_id = ?), ?, ?, ?)
    on conflict do nothing;
  )SQL");

  insert_q.bind(1,article_mid.c_str());
  insert_q.bind(2,number);
  insert_q.bind(3,part_mid);
  insert_q.bind(4,static_cast<int64_t>(bytes));
  int res = insert_q.exec();

  // update line_count only if the part is new
  if (res > 0) {
    SQLite::Statement update_q(pan_db,R"SQL(
      update article set line_count = line_count + ?
      where message_id = ?
    )SQL");
    update_q.bind(1, static_cast<int64_t>(lines));
    update_q.bind(2, article_mid.c_str());
    update_q.exec();
  }
}

namespace {
unsigned long long view_to_ull(StringView const &view)
{
  unsigned long long val(0ull);
  if (! view.empty())
  {
    errno = 0;
    val = strtoull(view.str, nullptr, 10);
    if (errno)
    {
      val = 0ull;
    }
  }
  return val;
}
} // namespace


// update part state of all articles of a given group. The state is
// updated only if it is null or incomplete
void DataImpl ::update_part_states(Quark const &group)
{
  TimeElapsed timer;

  SQLite::Statement q(pan_db,R"SQL(
    update article
    set part_state = case
       -- not a multipart
       when upd.binary is false
       then 'S'

       -- someone's posted a followup to a multipart
       when upd.line_count < 250 and upd.subject like 're: %'
       then 'S'

       --  someone's posted a "000/124" info message
       when upd.found_parts == 0
       then 'S'

       -- complete multipart
       when upd.found_parts == upd.expected_parts
       then 'C'

       -- incomplete multipart
       else 'I'
    end
    from (
       select a.id, binary, subject, line_count, expected_parts,
             (select count() from article_part as p2 where p2.article_id == a.id) as found_parts
       from article as a
       join article_group as ag on ag.article_id == a.id
       join `group`       as g  on ag.group_id == g.id
       join `subject`     as sb on sb.id == a.subject_id
       where g.name == ?
         and (part_state is null or part_state is "I")
    ) as upd
    where article.id == upd.id
  )SQL");

  q.bind(1,group.c_str());
  int count(q.exec());

  LOG4CXX_INFO(logger, "Updated " << count << " articles part states of group " << group.c_str()
               << " in " << timer.get_seconds_elapsed() << "s.");
}

// load headers from internal file in ~/.pan2/groups
void DataImpl ::migrate_headers(DataIO const &data_io, Quark const &group)
{
  TimeElapsed timer;

  StringView line;
  bool success(false);
  quarks_t servers;
  int item_count(0);
  int article_count(0);

  ArticleFilter::sections_t score_sections;
  _scorefile.get_matching_sections(StringView(group), score_sections);

  SQLite::Statement set_article_q(pan_db,R"SQL(
    insert into `article` (flag, message_id,subject_id, author_id, `references`,
                           time_posted, binary, expected_parts,line_count)
    values (?,?,(select id from subject where subject = ?),
            (select id from author where author = ?),?,?,?,?,?) on conflict do nothing;
  )SQL");

  SQLite::Statement set_subject_q(pan_db, R"SQL(
    insert into `subject` (subject)
    values ($subject)
    on conflict (subject) do nothing
  )SQL");

  SQLite::Statement set_author_q(pan_db,R"SQL(
    insert into `author` (author) values (?) on conflict do nothing;
  )SQL");

  SQLite::Statement set_article_group_q(pan_db,R"SQL(
    insert into `article_group` (article_id, group_id)
    values (
      (select id from article where message_id = ?),
      (select id from `group` where name = ?)
    ) on conflict (article_id, group_id) do nothing;
  )SQL");

  SQLite::Statement set_xref_q(pan_db,R"SQL(
    insert into `article_xref` (article_group_id, server_id, number)
    values (
      (select ag.id from article_group as ag
       join `group` as g on g.id == ag.group_id
       join article as a on a.id == ag.article_id
       where a.message_id = ? and g.name = ?),
      (select id from server where pan_id = ?),
      ?
    ) on conflict (article_group_id, server_id) do nothing;
  )SQL");

  SQLite::Statement set_part_q(pan_db,R"SQL(
    insert into `article_part` (article_id, part_number, part_message_id, size)
    values ((select id from article where message_id = ?), ?, ?, ?) on conflict do nothing;
  )SQL");

  LOG4CXX_INFO(logger, "Migrating articles of group " << group.c_str()
               << " in DB. Please wait a few minutes.");

  char const *groupname(group.c_str());
  LineReader *in(data_io.read_group_headers(group));
  bool in_transaction(0);
  if (in && ! in->fail())
  {
    do
    { // skip past the comments at the top
      in->getline(line);
      line.trim();
    } while (! line.empty() && *line.str == '#');

    int const version(atoi(line.str));
    if (version == 1 || version == 2 || version == 3)
    {
      // build the symbolic server / group lookup table
      in->getline(line);
      int symbol_qty(atoi(line.str));
      Quark xref_lookup[CHAR_MAX];
      for (int i = 0; i < symbol_qty; ++i)
      {
        StringView key;
        in->getline(line);
        line.trim();
        if (line.pop_token(key, '\t') && key.len == 1)
        {
          xref_lookup[(int)*key.str] = line;
        }
      }

      // build the author lookup table
      in->getline(line);
      symbol_qty = atoi(line.str);
      Quark author_lookup[CHAR_MAX];
      for (int i = 0; i < symbol_qty; ++i)
      {
        StringView key;
        in->getline(line);
        line.trim();
        if (line.pop_token(key, '\t') && key.len == 1)
        {
          author_lookup[(int)*key.str] = line;
        }
      }

      Xref::targets_t targets;
      std::vector<Xref::Target> &targets_v(targets.get_container());

      // each article in this group...
      unsigned int expire_count(0);
      in->getline(line);

      const time_t now(time(nullptr));
      PartBatch part_batch;

      for (;;)
      {
        if ((item_count & 0x3FFF) == 0) {
          if (item_count != 0) {
            LOG4CXX_INFO(logger, "Migrated " << item_count <<
                         " items of group " << group.c_str());
          }
          if (in_transaction)
            pan_db.exec("end transaction");

          pan_db.exec("begin transaction");
          in_transaction = true;
        }

        // look for the beginning of an Article record.
        // it'll be a message-id line with no leading whitespace.
        StringView s;
        if (! in->getline(s)) // end of file
        {
          break;
        }

        set_article_q.reset();
        set_subject_q.reset();
        set_author_q.reset();

        int bind_idx = 1;

        // flag line
        bool flag = false;
        if (version == 3)
        {
          flag = atoi(s.str) == 1 ? true : false;
          in->getline(s);
        }
        set_article_q.bind(bind_idx++, flag);

        if (s.empty() || *s.str != '<') // not a message-id...
        {
          continue;
        }

        // message id
        s.ltrim();
        std::string message_id(s);
        set_article_q.bind(bind_idx++, message_id.c_str());

        // subject line
        in->getline(s);
        s.ltrim();
        set_article_q.bind(bind_idx++, std::string{s}.c_str());
        set_subject_q.bind(1, std::string{s}.c_str());

        // author line
        in->getline(s);
        s.ltrim();
        // set author in author table
        StringView author = s.len == 1 ? author_lookup[(int)*s.str].to_view() : s;
        author.trim();
        set_author_q.bind(1,author);
        set_author_q.exec();
        // set author id in article table
        set_article_q.bind(bind_idx++,author);

        // optional references line
        std::string references;
        in->getline(s);
        s.ltrim();
        if (! s.empty() && *s.str == '<')
        {
          references = s;
          in->getline(s);
          s.ltrim();
        }
        if (references.empty())
        {
          set_article_q.bind(bind_idx++); // bind null
        }
        else
        {
          set_article_q.bind(bind_idx++, references.c_str());
        }

        // date-posted line
        unsigned long long time_posted = view_to_ull(s);
        set_article_q.bind(bind_idx++, static_cast<int64_t>(time_posted));

        // xref line
        in->getline(s);
        s.ltrim();
        size_t const max_targets(std::count(s.begin(), s.end(), ' ') + 1);
        targets_v.resize(max_targets);
        Xref::Target *target_it(&targets_v.front());
        StringView tok, server_tok, group_tok;
        while (s.pop_token(tok))
        {
          if (tok.pop_token(server_tok, ':') && tok.pop_token(group_tok, ':'))
          {
            target_it->server = server_tok;
            target_it->group = group_tok.len == 1 ?
                                 xref_lookup[(int)*group_tok.str] :
                                 Quark(group_tok);
            target_it->number = Article_Number(tok);
            Server const *server(find_server(target_it->server));
            if (server)
            {
              ++target_it;
            }
          }
        }
        targets_v.resize(target_it - &targets_v.front());
        targets.sort();

        // is_binary [total_part_count found_part_count] lines
        int total_part_count(1);
        int found_part_count(1);
        in->getline(s);
        s.ltrim();
        s.pop_token(tok);
        bool is_binary = ! tok.empty() && tok.str[0] == 't';
        set_article_q.bind(bind_idx++, is_binary);

        if (is_binary)
        {
          s.ltrim();
          s.pop_token(tok);
          total_part_count = atoi(tok.str);
          s.ltrim();
          s.pop_token(tok);
          found_part_count = atoi(tok.str);
        }
        set_article_q.bind(bind_idx++,
                           total_part_count); // expected_parts in table

        s.ltrim();
        int lines = 0;
        if (s.pop_token(tok))
        {
          lines = atoi(tok.str); // this field was added in 0.115
        }
        set_article_q.bind(bind_idx++, lines); // line_count in table

        // at this point we have all required information to create article in
        // DB
        set_subject_q.exec();
        set_article_q.exec();
        item_count++;
        article_count++;
        LOG4CXX_TRACE(logger, "Stored article " << message_id);

        // Then xref data can also be stored in DB
        foreach_const (Xref::targets_t, targets, it)
        {
          // store only xref to migrated group. Xref to other groups
          // can be retrieved via article table and
          // message_id. Otherwise we get articles in unsubscribed
          // groups, which is confusing after migration.
          if (it->group != group) {
            continue;
          }
          // create group if it's unknown
          add_group_in_db(it->server, it->group);

          // then the xref can be stored
          set_article_group_q.reset();
          set_article_group_q.bind(1,message_id);
          set_article_group_q.bind(2,it->group);
          set_article_group_q.exec();

          set_xref_q.reset();
          set_xref_q.bind(1, message_id.c_str());
          set_xref_q.bind(2, it->group.c_str());
          set_xref_q.bind(3, it->server.c_str());
          set_xref_q.bind(4, static_cast<int64_t>(it->number));
          set_xref_q.exec();
          item_count++;
          LOG4CXX_TRACE(logger, "article " << message_id << " stored xref in group "
                        << it->group.c_str());
        }

        // found parts...
        LOG4CXX_TRACE(logger, "article " << message_id << " found " << found_part_count
                      << " out of " << total_part_count);
        for (int i(0), count(found_part_count); i < count; ++i)
        {
          bool const gotline(in->getline(s));

          if (gotline)
          {
            set_part_q.reset();
            set_part_q.bind(1, message_id);

            StringView tok;
            s.ltrim();
            s.pop_token(tok);
            int const part_number(atoi(tok.str));
            if (part_number > total_part_count)
            { // corrupted entry
              break;
            }
            set_part_q.bind(2, part_number);

            StringView part_mid;
            unsigned long part_bytes(0);
            s.ltrim();
            s.pop_token(part_mid);
            if (part_mid.len == 1 && *part_mid.str == '"')
            {
              part_mid = message_id;
            }
            set_part_q.bind(3, part_mid);
            s.pop_token(tok);

            part_bytes = view_to_ull(tok);
            set_part_q.bind(4, static_cast<int64_t>(part_bytes));

            set_part_q.exec();
          }
        }

        // Now the article is loaded in DB. It's time to score it
        Article a(group, message_id);
        _header_filter.score_article(*this,score_sections,a);
      }

      success = ! in->fail();
    }
    else
    {
      Log::add_urgent_va(_("Unsupported data version for %s headers: %d.\nAre "
                           "you running an old version of Pan by accident?"),
                         groupname,
                         version);
    }
  }
  delete in;

  SQLite::Statement get_article_with_references(pan_db, R"SQL(
    select message_id, `references` from `article` as a
    join article_group as ag on ag.article_id == a.id
    join article_xref as x on x.article_group_id == ag.id
    join `group` as g on ag.group_id == g.id
    where g.name = ? and `references` is not null
    order by time_posted
  )SQL");
  get_article_with_references.bind(1,group);

  while (get_article_with_references.executeStep())
  {
    Quark msg_id(get_article_with_references.getColumn(0).getText());
    std::string a_ref(get_article_with_references.getColumn(1).getText());
    store_parent_articles(msg_id, a_ref);
  }

  if (in_transaction) {
    pan_db.exec("end transaction");
  }

  int part_count = item_count - article_count;
  LOG4CXX_INFO(logger, "Migrated " << article_count << " articles and "
                                   << part_count << " parts of groups "
                                   << group.c_str() << " in DB in "
                                   << timer.get_seconds_elapsed() << "s.");

  if (success)
  {
    double const seconds = timer.get_seconds_elapsed();
    Log::add_info_va(
      _("Loaded %llu articles for \"%s\" in %.1f seconds (%.0f per second)"),
      static_cast<uint64_t>(article_count),
      group.c_str(),
      seconds,
      static_cast<uint64_t>(article_count)
        / (fabs(seconds) < 0.001 ? 0.001 : seconds));
  }
}

void DataImpl::store_parent_articles(Quark &message_id, std::string &references)
{
  SQLite::Statement update_parent_in_article(pan_db, R"SQL(
      with p(p_id) as (
        select id from article where message_id == $parent_msg_id
      )
      update article
      set parent_id = (select p_id from p)
      where message_id == $current_msg_id and (select p_id from p) is not null
    )SQL");

  // insert a ghost article if real article is not found
  SQLite::Statement insert_ghost(pan_db, R"SQL(
      insert or ignore into ghost (ghost_msg_id)
      select $msg_id
      where (select count() from article where message_id == $msg_id) == 0
    )SQL");

  SQLite::Statement update_ghost_parent_id(pan_db, R"SQL(
      with p(p_id) as (
        select id from ghost where ghost_msg_id == ?
      )
      update article
      set ghost_parent_id = (select p_id from p)
      where message_id == ? and (select p_id from p) is not null
    )SQL");

  SQLite::Statement update_ghost(pan_db, R"SQL(
      update ghost
      set ghost_parent_msg_id = $parent_msg_id
      where ghost_msg_id == $msg_id
    )SQL");

  StringView current_msg_id(message_id), parent_msg_id, refs(references);
  LOG4CXX_TRACE(tree_logger,
                "references msg " << message_id << ": storing refs " << refs);

  // get newest reference first
  while (refs.pop_last_token(parent_msg_id, ' '))
  {
    parent_msg_id.trim();
    if (parent_msg_id.empty())
    {
      break;
    }

    LOG4CXX_TRACE(tree_logger,
                  "references msg " << message_id << ": current "
                                    << current_msg_id << " parent "
                                    << parent_msg_id);
    if (parent_msg_id != message_id.c_str())
    {
      int found(0);
      // try to insert current ref in child message if it exists
      update_parent_in_article.reset();
      update_parent_in_article.bind(1, parent_msg_id);
      update_parent_in_article.bind(2, current_msg_id);
      found = update_parent_in_article.exec();

      LOG4CXX_TRACE(tree_logger,
                    "references msg " << message_id << ": found " << found
                                      << " real article");
      if (found != 0)
      {
        // real parent is found, so there's no need to explore further the
        // references
        break;
      }

      // insert ghost message linked to current real article
      // ghost message must also store its parent message_id (ghost or not
      // ghost)
      insert_ghost.reset();
      insert_ghost.bind(1, parent_msg_id);
      int inserted = insert_ghost.exec();
      LOG4CXX_TRACE(tree_logger,
                    "references msg " << message_id << ": added " << inserted
                                      << " ghost article for parent msg id "
                                      << parent_msg_id);

      // insert parent in ghost article
      update_ghost.reset();
      update_ghost.bind(1, parent_msg_id);
      update_ghost.bind(2, current_msg_id);
      found = update_ghost.exec();

      // update in real article
      update_ghost_parent_id.reset();
      update_ghost_parent_id.bind(1, parent_msg_id);
      update_ghost_parent_id.bind(2, current_msg_id);
      update_ghost_parent_id.exec();
    }
    else
    {
      LOG4CXX_TRACE(tree_logger,
                    "references msg " << message_id << ": skipped wrong ref "
                                      << parent_msg_id);
    }

    current_msg_id = parent_msg_id;
  }
}

void DataImpl::store_references(Quark message_id, std::string references)
{
  if (! references.empty())
  {
    store_parent_articles(message_id, references);
  }

  // now handle the case where a real article replaces a ghost article

  // first add the parent_id pointing to the new real article in the
  // real child articles that points to the ghost to be replaced
  SQLite::Statement set_current_parent_id(pan_db, R"SQL(
    with recursive p(id,msg_id) as (
     select id, ghost_msg_id from ghost where ghost_msg_id == $msg_id
     union all
     select g.id, ghost_msg_id from ghost as g join p on p.msg_id == g.ghost_parent_msg_id
	   limit 500 -- safety net
    )
    update article
      set parent_id = (select id from article where message_id = $msg_id)
	    where id in (select a.id from article as a join p on p.id = ghost_parent_id)
  )SQL");
  set_current_parent_id.bind(1, message_id);
  int changed = set_current_parent_id.exec();

  LOG4CXX_TRACE(tree_logger, "references msg " << message_id << ": changed " << changed << " real parent id");

  // found a real article, so any matching ghost article must be deleted
  SQLite::Statement delete_ghost(pan_db, R"SQL(
    delete from ghost where ghost_msg_id == ?
  )SQL");
  delete_ghost.bind(1, message_id);
  delete_ghost.exec();
  LOG4CXX_TRACE(tree_logger,
                "references msg " << message_id << ": delete ghost article");

  // now handle the case where a real article is added with some
  // references (stored above) and where one of these references is a
  // real article. In this case the parent id of the new article must
  // point to the first real article found in the reference chain
  SQLite::Statement insert_parent_from_ancestor(pan_db, R"SQL(
    with recursive p(ghost_id, msg_id) as (
      select g.id, ghost_parent_msg_id from article as a
      join ghost as g on a.ghost_parent_id == g.id
      where message_id == $msg_id
      union all
      select id, ghost_parent_msg_id from ghost
      join p
      where ghost_msg_id == p.msg_id
      limit 1000
    )
    update article
    set parent_id = (select id from article as a join p on a.message_id == p.msg_id where true)
	    where ghost_parent_id == (select ghost_id from p)
  )SQL");
  insert_parent_from_ancestor.bind(1,message_id);
  changed = insert_parent_from_ancestor.exec();
  LOG4CXX_TRACE(tree_logger, "references msg " << message_id << ": changed " << changed << " real parent id to an ancestor");

  // safety check
  if (_debug_flag) {
    SQLite::Statement consistency_check(pan_db, R"SQL(
      select message_id from article as a
      join ghost as g on a.message_id == ghost_msg_id
    )SQL");

    int bad(0);
    while (consistency_check.executeStep()) {
      bad++;
      std::string bad_id = consistency_check.getColumn(1);
      LOG4CXX_TRACE(tree_logger, "found both real and ghost article: ");
    }
    assert(bad == 0);
  }
}

void DataImpl ::migrate_read_ranges(Quark const &group) {
  // for each serveur containing this group:
  //  foreach article in  read-range
  //     mark article as read

  LOG4CXX_INFO(logger,  "Migrating articles read status of group " << group.c_str()
               << " in DB.");

  SQLite::Statement read_group_q(pan_db, R"SQL(
    select s.id, g.id, read_ranges
    from `group` as g
    join server_group as sg on g.id == sg.group_id
    join server as s on s.id == sg.server_id
    where s.host != "local" and g.name = ?
  )SQL");

  SQLite::Statement get_articles_q(pan_db, R"SQL(
    select a.id, number
    from `article` as a
    join article_group as ag on a.id == ag.article_id
    join article_xref as xr on ag.id == xr.article_group_id
    where xr.server_id = ? and ag.group_id = ?
  )SQL");

  SQLite::Statement set_articles_as_read(pan_db, R"SQL(
    update `article` set is_read = True where id == ?
  )SQL");

  int server_id, group_id, article_id, article_count(0), read_count(0);
  Numbers ranges;

  read_group_q.bind(1,group.c_str());

  while(read_group_q.executeStep()) {
    server_id = read_group_q.getColumn(0);
    group_id = read_group_q.getColumn(1);
    ranges.clear();
    ranges.mark_str( read_group_q.getColumn(2).getText() );

    get_articles_q.reset();
    get_articles_q.bind(1, server_id);
    get_articles_q.bind(2, group_id);
    while (get_articles_q.executeStep()) {
      article_id = get_articles_q.getColumn(0);
      Article_Number article_nb( get_articles_q.getColumn(1).getInt64());
      article_count++;
      if (ranges.is_marked(article_nb)) {
        set_articles_as_read.reset();
        set_articles_as_read.bind(1, article_id);
        int res = set_articles_as_read.exec();
        assert(res == 1);
        read_count++;
      }
    }
  }

  LOG4CXX_DEBUG(logger, "Marked " << read_count << " articles as read (out of " << article_count << " articles)");
}


void DataImpl ::load_headers_from_db(Quark const &group) {
  TimeElapsed timer;

  GroupHeaders *h(get_group_headers(group));
  assert(h != nullptr);

  Article_Count article_count(0);
  StringView line;
  quarks_t servers;

  LOG4CXX_INFO(logger, "Loading headers for group " << group.c_str());

  SQLite::Statement set_current_group(pan_db, R"SQL(
     insert into current_group ( id, group_id )
          select 1, id from `group` as g where g.name = $grp
     on conflict (id) do
       update set group_id =
         (select id from `group` as g where g.name = $grp)
  )SQL");
  set_current_group.bind(1,group);
  set_current_group.exec();

  int group_id(0), total_article_count(0);

  SQLite::Statement group_info_q(pan_db, "select id, total_article_count from `group` where name = ?");
  group_info_q.bind(1, group.c_str());
  while (group_info_q.executeStep()) {
    group_id = group_info_q.getColumn(0);
    total_article_count = group_info_q.getColumn(1);
  }

  assert(group_id != 0);

  SQLite::Statement read_article_q(pan_db,R"SQL(
    select message_id, `references`
      from article
      join article_group as ag on ag.article_id = article.id
      where ag.group_id = ?
  )SQL");
  read_article_q.bind(1, group_id);

  // expire articles of one group. Since expiry days is attached to server (and not
  // group), an article must be expired when all its server show it as
  // expired. With this, an orphaned article (its last server was
  // removed) is also seen as expired.
  // I hate negative logic, but it's the only way to expire article
  // when it must be expired on all servers, i.e. do not expire an
  // article if it's not expired on any server.
  SQLite::Statement delete_expired_articles_q(pan_db,R"SQL(
    with goners ( article_id ) as (
      select a.id from article as a
      join article_group as ag on ag.article_id == a.id
      join `group` as g on ag.group_id == g.id
      where g.name == ?
        and
        (
          -- count **NOT** expired article parts
          select count()
          from article_xref  as ix
          join server        as i_s on ix.server_id == i_s.id
          join article_group as iag on ix.article_group_id == iag.id
          where iag.article_id == a.id
            and (
              -- server has no expiration set
              i_s.expiry_days == 0 or
              -- article is younger than expiration date
              julianday() - julianday(a.time_posted, 'unixepoch') < i_s.expiry_days
            )
         ) == 0 -- no *NOT* expired parts found
    )
    delete from article as a
      where a.id in (select article_id from goners)
  )SQL");

  delete_expired_articles_q.bindNoCopy(1, group.c_str());
  unsigned int expire_count(delete_expired_articles_q.exec());

  if (expire_count) {
    LOG4CXX_INFO(logger, "Expired " << expire_count << " articles from group " << group.c_str()
                  << "in " << timer.get_seconds_elapsed() << "s.");
    Log::add_info_va(_("Expired %lu old articles from \"%s\""),
                     expire_count,
                     group.c_str());
  }

  Article_Count const article_qty{static_cast<unsigned long>(total_article_count)};
  h->reserve(article_qty);

  const time_t now(time(nullptr));
  while (read_article_q.executeStep()) {
    Article &a(h->alloc_new_article());

    int i(0);
    char const *message_id = read_article_q.getColumn(i++);
    a.group = group;
    a.message_id = Quark(message_id);
    ++article_count;
  }

  fire_group_counts(group);

  double const seconds = timer.get_seconds_elapsed();
  Log::add_info_va(
                   _("Loaded %llu articles for \"%s\" in %.1f seconds (%.0f per second)"),
                   static_cast<uint64_t>(article_count),
                   group.c_str(),
                   seconds,
                   static_cast<uint64_t>(article_count)
                   / (fabs(seconds) < 0.001 ? 0.001 : seconds));

  LOG4CXX_DEBUG(logger, "Loaded " << article_count << " articles in " << timer.get_seconds_elapsed() << "s.");
}

namespace {
char const *lookup_symbols("abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "1234567890!@#$%^&*()");

struct QuarkToSymbol
{
    char buf[2];

    QuarkToSymbol()
    {
      buf[1] = '\0';
    }

    typedef Loki::AssocVector<pan::Quark, char> quark_to_symbol_t;
    quark_to_symbol_t _map;

    char const *operator()(Quark const &quark)
    {
      quark_to_symbol_t::const_iterator it(_map.find(quark));
      if (it == _map.end())
      {
        return quark.c_str();
      }

      buf[0] = it->second;
      return buf;
    }

    void write(std::ostream &out, StringView const &comment) const
    {
      Quark quarks[UCHAR_MAX];
      foreach_const (quark_to_symbol_t, _map, it)
      {
        quarks[(int)it->second] = it->first;
      }

      const size_t len(_map.size());
      out << len;
      if (! comment.empty())
      {
        out << "\t # " << comment << '\n';
      }
      for (size_t i(0); i != len; ++i)
      {
        char const ch(lookup_symbols[i]);
        out << '\t' << ch << '\t' << quarks[(int)ch] << '\n';
      }
    }
};

typedef Loki::AssocVector<Quark, unsigned long> frequency_t;

void build_qts(frequency_t &freq, QuarkToSymbol &setme)
{
  setme._map.clear();

  typedef std::multimap<unsigned long, Quark> counts_t;
  counts_t counts;
  foreach_const (frequency_t, freq, it)
  {
    counts.insert(std::pair<unsigned long, Quark>(it->second, it->first));
  }

  counts_t::const_reverse_iterator it = counts.rbegin(), end = counts.rend();
  for (char const *pch = lookup_symbols; *pch && it != end; ++pch, ++it)
  {
    setme._map[it->second] = *pch;
  }
  freq.clear();
}
} // namespace

/*******
********
*******/

void DataImpl ::mark_read(Article const &a, bool read)
{
  Article const *aptr(&a);
  mark_read(&aptr, 1, read);
}

void DataImpl ::mark_read(Article const **articles,
                          unsigned long article_count,
                          bool read)
{
  typedef std::map<Quark, quarks_t> group_to_changed_mids_t;
  group_to_changed_mids_t group_to_changed_mids;

  SQLite::Statement set_read_q(pan_db, R"SQL(
    update `article` set is_read = $read where message_id = ? and is_read != $read
  )SQL");

  SQLite::Statement read_group_q(pan_db, R"SQL(
    select g.name from `group` as g
    join article_group as ag on ag.group_id == g.id
    join article as a on ag.article_id == a.id
    where message_id = ?
  )SQL");

  // set them to `read'...
  for (Article const **it(articles), **end(articles + article_count); it != end;
       ++it)
  {
    Article const *article(*it);
    set_read_q.reset();
    set_read_q.bind(1, read);
    set_read_q.bindNoCopy(2, article->message_id);

    if (set_read_q.exec() == 1)
    {
      LOG4CXX_TRACE(logger,
                    "Changed read status of article " << article->message_id
                                                      << " to " << read);
      // get affected groups
      read_group_q.reset();
      read_group_q.bindNoCopy(1, article->message_id);
      while (read_group_q.executeStep())
      {
        Quark grp (Quark(read_group_q.getColumn(0).getText()));
        group_to_changed_mids[grp].insert(article->message_id);
      }
    }
  }

  // update the affected groups' unread counts...
  foreach_const (group_to_changed_mids_t, group_to_changed_mids, it)
  {
    Quark const &group(it->first);
    fire_group_counts(group);
    on_articles_changed(group, it->second, false);
  }
}

bool DataImpl ::is_read(Article const *a) const
{
  SQLite::Statement is_read_q(pan_db, R"SQL(
    select is_read from `article` where message_id = ?
  )SQL");

  is_read_q.bind(1, a->message_id.c_str());
  while (is_read_q.executeStep()) {
    return is_read_q.getColumn(0).getInt() == 1;
  }

  // article was not found, bail out
  LOG4CXX_FATAL(logger, "Article " << a->message_id.c_str() << " not found in DB");
  // trigger a core dump so the pb can be debugged
  assert(0);
}

void DataImpl ::get_article_scores(Quark const &group,
                                   Article const &article,
                                   Scorefile::items_t &setme) const
{
  ArticleFilter ::sections_t sections;
  _scorefile.get_matching_sections(StringView(group), sections);
  _article_filter.get_article_scores(*this, sections, group, article, setme);
}

// Compute the score of a list of articles
void DataImpl ::rescore_articles(Quark const &group, const quarks_t mids)
{
  ArticleFilter::sections_t sections;
  _scorefile.get_matching_sections(group.to_view(), sections);
  foreach (quarks_t, mids, it)
  {
    Article a(group, *it);
    // score only if article is a real article, i.e. the article part
    // whose msg_id matches an article message id. A plain article
    // part is not stored in article table but in article_part.
    if (a.is_in_db_article_table())
      _header_filter.score_article(*this, sections, a);
  }
}

// compute the score of all articles of a group
void DataImpl ::rescore_group_articles(Quark const &group)
{
  ArticleFilter::sections_t sections;
  _scorefile.get_matching_sections(group.to_view(), sections);

  SQLite::Statement q(pan_db, R"SQL(
    select message_id from `article` as a
    join article_group as ag on ag.article_id == a.id
    join `group` as g on g.id == ag.group_id
    where g.name = ?
  )SQL");

  q.bind(1, group.c_str());
  while (q.executeStep()) {
    Article a(group, Quark(q.getColumn(0).getText()));
    _header_filter.score_article(*this, sections, a);
  }
}

void DataImpl ::rescore()
{
  // std::cerr << LINE_ID << " rescoring... " << std::endl;
  const std::string filename(_data_io->get_scorefile_name());

  // reload the scorefile...
  _scorefile.clear();
  _scorefile.parse_file(filename);

  // enumerate the groups that need rescoring...
  quarks_t groups;
  foreach (std::set<MyTree *>, _trees, it)
  {
    groups.insert((*it)->_group);
  }

  // "on_articles_changed" rescores the articles...
  foreach_const (quarks_t, groups, git)
  {
    quarks_t mids;
    Quark const &group(*git);
    GroupHeaders const *h(get_group_headers(group));
    foreach_const (nodes_t, h->_nodes, nit)
    {
      // only insert mids for nodes with articles
      if (nit->second->_article)
      {
        mids.insert(mids.end(), nit->first);
      }
    }
    if (! mids.empty())
    {
      on_articles_changed(group, mids, true);
    }
  }
}

void DataImpl ::add_score(StringView const &section_wildmat,
                          int score_value,
                          bool score_assign_flag,
                          int lifespan_days,
                          bool all_items_must_be_true,
                          Scorefile::AddItem const *items,
                          size_t item_count,
                          bool do_rescore)
{
  const std::string filename(_data_io->get_scorefile_name());

  if (item_count && items)
  {
    // append to the file...
    const std::string str(_scorefile.build_score_string(section_wildmat,
                                                        score_value,
                                                        score_assign_flag,
                                                        lifespan_days,
                                                        all_items_must_be_true,
                                                        items,
                                                        item_count));
    std::ofstream o(filename.c_str(), std::ofstream::app | std::ofstream::out);
    o << '\n' << str << '\n';
    o.close();
    chmod(filename.c_str(), 0600);
  }

  if (do_rescore)
  {
    rescore();
  }
}

void DataImpl ::comment_out_scorefile_line(StringView const &filename,
                                           size_t begin_line,
                                           size_t end_line,
                                           bool do_rescore)
{
  std::string buf;

  // read the file in...
  std::string line;
  std::ifstream in(filename.to_string().c_str());
  size_t line_number(0);
  while (std::getline(in, line))
  {
    ++line_number;
    if (begin_line <= line_number && line_number <= end_line)
    {
      buf += '#';
    }
    buf += line;
    buf += '\n';
  }
  in.close();

  // ..and back out again
  const std::string f(filename.str, filename.len);
  std::ofstream o(f.c_str(), std::ofstream::trunc | std::ofstream::out);
  o << buf;
  o.close();
  chmod(f.c_str(), 0600);

  // rescore
  if (do_rescore)
  {
    rescore();
  }
}

/***************************************************************************
****************************************************************************
***************************************************************************/

namespace {
/** used by delete_articles */
struct PerGroup
{
    quarks_t mids;
    Article_Count unread;
    Article_Count count;

    PerGroup() :
      unread(0),
      count(0)
    {
    }
};
} // namespace

void DataImpl ::group_clear_articles(Quark const &group)
{
  // if they're in memory, remove them from there too...
  GroupHeaders *headers(get_group_headers(group));
  if (headers)
  {
    unique_articles_t all;
    foreach (nodes_t, headers->_nodes, it)
    {
      if (it->second->_article)
      {
        all.insert(it->second->_article);
      }
    }
    delete_articles(all);
  }

  // reset GroupHeaders' memory...
  //  headers->_nodes.clear ();
  //  headers->_node_chunk.clear ();
  //  headers->_art_chunk.clear ();

  // remove 'em from DB too. delete the article_group entry.
  SQLite::Statement delete_group_q(pan_db, R"SQL(
    delete from `article_group` where group_id == (select id from `group` where name == ?);
  )SQL" );
  delete_group_q.bind(1,group.c_str());
  int count = delete_group_q.exec();

  LOG4CXX_TRACE(logger,  "Deleted all " << count << " xref articles of group "
                << group.c_str() << " in DB.");

  // delete orphan article, i.e. article not in article_group
  SQLite::Statement delete_article_q(pan_db, R"SQL(
    delete from `article` where id in (
      select distinct a.id from `article` as a
      left outer join article_group as ag on ag.article_id == a.id
      where ag.article_id is null
    )
  )SQL");
  count = delete_article_q.exec();

  // count may be different because of cross-posting or previously orphaned articles
  LOG4CXX_TRACE(logger,  "Deleted all " << count << " articles of group "
                << group.c_str() << " in DB.");

  fire_group_counts(group);
}

void DataImpl ::delete_articles(unique_articles_t const &articles) {
  std::vector<Article> art;
  for (Article const *a : articles) {
    art.push_back(*a);
  }
  delete_articles(art);
}

void DataImpl ::delete_articles(std::vector<Article> const &articles)
{
  quarks_t all_mids;

  LOG4CXX_DEBUG(logger,  "Deleting " << articles.size() << " articles from DB.");

  // info we need to batch these deletions per group...
  typedef std::map<Quark, PerGroup> per_groups_t;
  per_groups_t per_groups;

  SQLite::Statement get_group_q(pan_db, R"SQL(
    select name from `group` as g
    join article_group as ag on ag.group_id == g.id
    join article as a on ag.article_id == a.id
    where a.message_id == ?
  )SQL");

  for (Article article: articles)
  {
    // populate the per_groups map
    quarks_t groups;
    get_group_q.reset();

    get_group_q.bindNoCopy(1, article.message_id.c_str());
    while (get_group_q.executeStep()) {
      std::string grp = get_group_q.getColumn(0);
      groups.insert(grp);
    }

    bool const was_read(is_read(&article));
    foreach_const (quarks_t, groups, git)
    {
      PerGroup &per(per_groups[*git]);
      ++per.count;
      if (! was_read)
      {
        ++per.unread;
      }
      per.mids.insert(article.message_id);
      all_mids.insert(article.message_id);
    }

    all_mids.insert(article.message_id);
  }

  // process each group
  foreach (per_groups_t, per_groups, it)
  {
    // update the group's read/unread count...
    Quark const &group(it->first);
    fire_group_counts(group);

    // remove the articles from our lookup table...
    GroupHeaders *h(get_group_headers(group));
    if (h)
    {
      h->remove_articles(it->second.mids);
    }
  }

  // mark articles as pending deletion
  SQLite::Statement mark_to_delete_q(pan_db, R"SQL(
    update article set to_delete = True where message_id == ?
  )SQL");

  for (Quark msg_id: all_mids) {
    mark_to_delete_q.reset();

    mark_to_delete_q.bindNoCopy(1, msg_id);
    assert(mark_to_delete_q.exec() == 1);

    LOG4CXX_TRACE(logger, "Marked article to delete " << msg_id);
  }

  // delete orphan authors
  SQLite::Statement author_q(pan_db, R"SQL(
    delete from author
    where id in
      (
        select distinct author.id from author
        left outer join article on author.id == article.author_id
        where article.author_id is null
      )
      and id not in
      (
        select author_id from profile
      )
  )SQL");
  author_q.exec();
}

// called when articles are read, rescored or added
void DataImpl ::on_articles_changed(Quark const &group,
                                    quarks_t const &mids,
                                    bool do_refilter)
{
  rescore_articles(group, mids);

  // notify the trees that the articles have changed...
  foreach (std::set<MyTree *>, _trees, it)
  {
    (*it)->articles_changed(do_refilter);
  }
}

void DataImpl ::on_articles_added(Quark const &group, quarks_t const &mids)
{

  if (! mids.empty())
  {
    Log::add_info_va(
      _("Added %lu articles to %s."), mids.size(), group.c_str());

    rescore_articles(group, mids);

    foreach (std::set<MyTree *>, _trees, it)
    {
      if ((*it)->_group == group)
      {
        LOG4CXX_DEBUG(logger,"Adding " << mids.size() << " articles to group " << group);
        (*it)->add_articles(mids);
      }
    }

    fire_group_counts(group);
  }
}

Data::ArticleTree *DataImpl ::group_get_articles(Quark const &group,
                                                 Quark const &save_path,
                                                 const ShowType show_type,
                                                 FilterInfo const *filter,
                                                 RulesInfo const *rules) const
{
  // cast const away for group_ref()... consider _groups mutable
  return new MyTree(
    *const_cast<DataImpl *>(this), group, save_path, show_type, filter, rules);
}
