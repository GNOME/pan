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

/**************
***************
**************/

#include "pan/general/article_number.h"
#include "pan/general/quark.h"
#include "pan/general/string-view.h"
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>
#include <config.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <log4cxx/logger.h>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <glib.h>
#include <glib/gi18n.h>
extern "C" {
  #include <unistd.h>
}

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/log4cxx.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/time-elapsed.h>

#include <pan/usenet-utils/numbers.h>

#include "data-impl.h"

using namespace pan;

/**
***
**/

namespace
{
log4cxx::LoggerPtr logger = pan::getLogger("group");

bool parse_newsrc_line(StringView const &line,
                       StringView &setme_group_name,
                       bool &setme_subscribed,
                       StringView &setme_number_ranges)
{
  bool ok(false);
  // since most groups will be unread, it's faster to test the end of the line
  // before calling strpbrk
  char const *delimiter(
    (! line.empty() && (line.back() == '!' || line.back() == ':')) ?
      &line.back() :
      line.strpbrk("!:"));
  if (delimiter)
  {
    StringView myline(line);

    setme_subscribed = *delimiter == ':';

    myline.substr(NULL, delimiter, setme_group_name);
    setme_group_name.trim();

    myline.substr(delimiter + 1, NULL, setme_number_ranges);
    setme_number_ranges.trim();

    ok = ! setme_group_name.empty();
  }

  return ok;
  }
}

// detect std::lib
#include <ciso646>
#ifdef _LIBCPP_VERSION
// using libc++
#include <algorithm>
#else
// using libstdc++
#include <ext/algorithm>
#endif

void DataImpl ::migrate_newsrc(Quark const &server_name, LineReader *in) {
  TimeElapsed timer;
  Server * s = find_server (server_name);
  if (!s) {
    Log::add_err_va (_("Skipping newsrc file for server \"%s\""), server_name.c_str());
    return;
  }

  std::vector<Quark>& groups (s->groups.get_container());

  Server const *server = find_server(server_name);
  LOG4CXX_DEBUG(logger, "saving groups of server " << server->host << " in DB");

  SQLite::Statement get_id_q(pan_db, "select id from server where host = ?");
  get_id_q.bind(1, server->host );
  int server_id;
  while (get_id_q.executeStep()) {
    server_id = get_id_q.getColumn(0);
  }

  std::stringstream create_st;
  create_st << "insert into `group` (name, subscribed) values (?,?) on conflict do nothing; ";
  SQLite::Statement create_q(pan_db,create_st.str());

  std::stringstream link_st;
  link_st << "insert into `server_group` (server_id, group_id, read_ranges) "
          << "select ?,`group`.id,? from `group` where `group`.`name` = ?;";
  SQLite::Statement link_q(pan_db,link_st.str());

  StringView line, name, numbers;
  int count = 0;
  while (!in->fail() && in->getline (line))
  {
    bool subscribed;
    if (parse_newsrc_line (line, name, subscribed, numbers))
    {
      SQLite::Transaction store_group_transaction(pan_db);

      create_q.reset();
      create_q.bind(1,name);
      create_q.bind(2,subscribed);
      create_q.exec();

      link_q.reset();
      link_q.bind(1, server_id);
      link_q.bind(2,numbers);
      link_q.bind(3, name);
      link_q.exec();

      store_group_transaction.commit();
      count++;
    }
  }

  double const seconds = timer.get_seconds_elapsed();

  LOG4CXX_INFO(logger, "Migrated " << count << " groups of server " << server->host
                << " in DB in " << seconds << "s.");
}

void DataImpl ::migrate_newsrc_files(DataIO const &data_io)
{
  foreach_const (servers_t, _servers, sit) {
    const Quark key (sit->first);
    const std::string filename = file::absolute_fn("", sit->second.newsrc_filename);
    LOG4CXX_DEBUG(logger, "reading " << sit->second.host << " groups from " << filename);
    if (file::file_exists (filename.c_str())) {
      LineReader * in (data_io.read_file (filename));
      migrate_newsrc (key, in);
      delete in;
      // remove obsolete file
      std::remove(filename.c_str());
    }
  }
}

// create a minimal group in DB. Does nothing if the group exists
void DataImpl ::add_group_in_db(StringView const &server_pan_id, StringView const &group, bool pseudo) {
  Quark s (server_pan_id.to_string());
  Quark g (group.to_string());
  add_group_in_db(s,g, pseudo);
}

// create a minimal group in DB. Update Pseudo property if the group exists.
void DataImpl ::add_group_in_db(Quark const &server_pan_id, Quark const &group, bool pseudo) {
  SQLite::Statement check_group_q(pan_db,"select pseudo from `group` where name = ?");

  // check if referenced group exists
  check_group_q.bind(1,group);
  int count(0);
  bool is_pseudo(false);
  while (check_group_q.executeStep()) {
    is_pseudo = check_group_q.getColumn(0).getInt();
    count++;
  }

  if (count == 1) {
    if (is_pseudo && !pseudo) {
      // the group is not pseudo after all, so reset the pseudo property of group
      SQLite::Statement update_group_q(pan_db,
                                       "update `group` set pseudo = False where name == ?");
      update_group_q.bind(1,group);
      update_group_q.exec();
    }

    // no further change is needed.
    return;
  }

  // new group add it as a pseudo or not pseudo group
  SQLite::Statement insert_group_q(pan_db,"insert into `group` (name, pseudo) values (?,?)");
  insert_group_q.bind(1,group);
  insert_group_q.bind(2,pseudo);
  insert_group_q.exec();

  SQLite::Statement insert_group_server_q(pan_db,R"SQL(
      insert into server_group (server_id, group_id) values (
        (select id from `server` where pan_id = ?),
        (select id from `group` where name = ?)
      )
  )SQL");

  insert_group_server_q.bind(1,server_pan_id);
  insert_group_server_q.bind(2,group);
  insert_group_server_q.exec();
}

void DataImpl ::load_groups_from_db() {
  StringView line, name, numbers;

  // sqlitebrowser returns 110k groups in 210ms (57 without ordering)
  SQLite::Statement read_group_q(pan_db,R"SQL(
    select name, read_ranges
      from `group` as g join server as s, server_group as sg
      where s.host = ? and s.id == sg.server_id and g.id == sg.group_id
            and s.host != "local" and g.pseudo == False
      order by name asc;
)SQL");

  // load groups of each server
  foreach_const (servers_t, _servers, sit) {
    TimeElapsed timer;
    Quark const &server(sit->first);
    Server * s = find_server (server);
    std::vector<Quark>& groups (s->groups.get_container());
    int i = 0;

    read_group_q.reset();
    read_group_q.bind(1, s->host);

    while (read_group_q.executeStep()) {
      i++;
      name = read_group_q.getColumn(0).getText();
      numbers = read_group_q.getColumn(1).getText();

      Quark const &group(name);
      groups.push_back (group);

      if (!numbers.empty())
        _read_groups[group][server]._read.mark_str (numbers);

    }
    LOG4CXX_DEBUG(logger, "Loaded " << i << " groups from server with pan_id " << server.c_str() << " from DB in "
                  << timer.get_seconds_elapsed() << "s.");
  }
  fire_grouplist_rebuilt ();
}

void DataImpl::save_new_groups_in_db(Quark const &server_pan_id, NewGroup const *newgroups, int count) {
  TimeElapsed timer;
  LOG4CXX_DEBUG(logger, "Saving " << count << " new groups in DB");

  // get server id
  SQLite::Statement get_server_id_q(pan_db, "select id from server where pan_id = ?");
  get_server_id_q.bind(1, server_pan_id.c_str() );
  int server_id(0);
  while (get_server_id_q.executeStep()) {
    server_id = get_server_id_q.getColumn(0);
  }
  assert(server_id > 0);

  LOG4CXX_DEBUG(logger, "Saving new groups in DB. Stored old groups: " << timer.get_seconds_elapsed() << " s");

  // get current list of group of this server
  SQLite::Statement get_group_ids_q(pan_db, R"SQL(
    select g.name, g.id from `group` as g join `server_group` as sg
      where sg.group_id = g.id and sg.server_id = ? and g.pseudo == False
)SQL");
  std::map <std::string, int> old_groups;
  get_group_ids_q.bind(1, server_id );
  while (get_group_ids_q.executeStep()) {
    old_groups[get_group_ids_q.getColumn(0)] = get_group_ids_q.getColumn(1);
  }

  SQLite::Statement store_group_q(pan_db, R"SQL(
    insert into `group` (name, subscribed) values (?, 0);
)SQL");

  SQLite::Statement store_link_q(pan_db,R"SQL(
    insert  into `server_group` (server_id, group_id)
      select ?, id from `group` where name = ? ;
)SQL");

  SQLite::Statement store_permission_q(pan_db, R"SQL(
    update `group` set permission = ?, pseudo = False where name == ?;
)SQL");

  SQLite::Statement set_desc_q(pan_db,R"SQL(
    insert into `group_description` (group_id, description)
      --  add new row with description
      select id, @desc from `group` where name = @name
      -- clobber old description if different
      on conflict (group_id) do update set description = @desc where description != @desc ;
  )SQL");

  LOG4CXX_DEBUG(logger, "Saving new groups in DB. Prepared statements: " << timer.get_seconds_elapsed() << " s");

  int added(0);
  for (NewGroup const *it = newgroups, *end = newgroups + count; it != end; ++it) {
    std::string name (it->group.c_str());

    if (old_groups.count(name) == 0) {
      LOG4CXX_TRACE(logger, "Adding new group " << name << " permission " << it->permission );
      SQLite::Transaction store_group_transaction(pan_db);

      // insert new group
      store_group_q.reset();
      store_group_q.bind(1,name);
      store_group_q.exec();

      // store server/group link
      store_link_q.reset();
      store_link_q.bind(1,server_id);
      store_link_q.bind(2,name);
      store_link_q.exec();

      store_group_transaction.commit();

      added++;
    }
    else {
      LOG4CXX_TRACE(logger, "Updating group " << name << " permission " << it->permission );
      old_groups.erase(name);
    }

    // store permission
    char perm (it->permission);
    if (perm == 'n' || perm == 'm' || perm == 'y') {
      std::string perm(1, it->permission);
      store_permission_q.reset();
      store_permission_q.bind(1, perm);
      store_permission_q.bind(2, name);
      store_permission_q.exec();
    } else {
      // Some server give unexpected permission like 'x' for intel.motherboards.pentium group on news.free.fr
      LOG4CXX_DEBUG(logger, "Skipping permission '" << perm << "' of group " << name << " server pan_id " << server_pan_id.c_str() );
    }

    // store description (if any)
    if (!it->description.empty() && it->description!="?" && it->description != name) {
      set_desc_q.reset();
      set_desc_q.bind(1, it->description);
      set_desc_q.bind(2, name);
      set_desc_q.exec();
    }
  }

  LOG4CXX_DEBUG(logger, "Saving new groups in DB. Checked or saved groups: " << timer.get_seconds_elapsed() << " s");

  int deleted(0);
  SQLite::Statement delete_group_q(pan_db, R"SQL(
    delete from `server_group`
      where server_id = ?
        and pseudo == False
        and group_id = (select id from `group` where name = ?)
  )SQL");

  // delete old group no longer found on server (does this ever
  // happens ?), i.e.  delete group_id from server_group. Group in
  // group table is actually deleted by delete_orphan_group trigger if
  // no other server provides it
  for (auto const& grp : old_groups) {
    LOG4CXX_DEBUG(logger, "Deleting obsolete group: " << grp.first);
    delete_group_q.reset();
    delete_group_q.bind(1, server_id);
    delete_group_q.bind(2, grp.first);
    int count = delete_group_q.exec();
    assert(count == 1);
    deleted++;
  }

  LOG4CXX_DEBUG(logger, "Saving new groups in DB. Deleted obsolete groups: " << timer.get_seconds_elapsed() << " s");

  LOG4CXX_INFO(logger, "Added " << added << " and deleted " << deleted <<
               " groups in " << timer.get_seconds_elapsed() << "s.");
}

void DataImpl::save_group_in_db(Quark const &server_name) {
  TimeElapsed timer;
  std::string newsrc_string;
  Server const *server = find_server(server_name);

  LOG4CXX_INFO(logger, "Saving groups of server " << server->host << " in DB...") ;

  // get server id
  SQLite::Statement get_id_q(pan_db, "select id from server where host = ?");
  get_id_q.bind(1, server->host );
  int server_id;
  while (get_id_q.executeStep()) {
    server_id = get_id_q.getColumn(0);
  }

  std::stringstream link_st;
  link_st << "with ids(gid) as (select id from `group` where name = $gname) "
          << "insert  into `server_group` (server_id, group_id, read_ranges) select $sid, gid, $rg from ids where true "
          << "on conflict (server_id, group_id) do update set read_ranges = $rg;";
  SQLite::Statement link_q(pan_db,link_st.str());

  // overly-complex optimization: both sit->second.groups and _subscribed
  // are both ordered by AlphabeticalQuarkOrdering.
  // Where N==sit->second.groups.size() and M==_subscribed.size(),
  // "bool subscribed = _subscribed.count (group)" is N*log(M),
  // but a sorted set comparison is M+N comparisons.
  AlphabeticalQuarkOrdering o;

  // for the groups in this server...
  int count = 0;
  foreach_const (Server::groups_t, server->groups, group_iter) {
    Quark const &group(*group_iter);

    // if the group's been read, save its read number ranges...
    ReadGroup::Server const *rgs(find_read_group_server(group, server_name));
    newsrc_string.clear ();
    if (rgs != nullptr) {
      rgs->_read.to_string (newsrc_string);
    }

    link_q.reset();
    link_q.bind(1,group_iter->c_str());
    link_q.bind(2,server_id);
    link_q.bind(3,newsrc_string);
    link_q.exec();

    count++;
  }

  double const seconds = timer.get_seconds_elapsed();

  LOG4CXX_INFO(logger, "Saved " << count << " groups "
               << "in " << seconds << "s in DB.");
}


void
DataImpl :: save_all_server_groups_in_db ()
{
  if (newsrc_autosave_id) {
    g_source_remove( newsrc_autosave_id );
    newsrc_autosave_id = 0;
  }

  if (_unit_test)
    return;

  // save all the servers' newsrc files
  foreach_const (servers_t, _servers, sit)
  {
    Quark const &server(sit->first);
    save_group_in_db(server);
  }
}

/***
****
***/

void DataImpl ::migrate_group_permissions(DataIO const &data_io)
{
  TimeElapsed timer;
  LineReader * in (data_io.read_group_permissions ());
  StringView s, line;

  SQLite::Statement save_perm_q(pan_db, "update `group` set permission = ? where name == ?");

  int count = 0;
  while (in && !in->fail() && in->getline(line))
  {
    if (line.len && *line.str=='#')
      continue;

    else if (!line.pop_last_token (s, ':') || !s.len || (*s.str!='y' && *s.str!='n' && *s.str!='m')) {
      std::cerr << LINE_ID << " Group permissions: Can't parse line `" << line << std::endl;
      continue;
    }

    const Quark group (line);
    const Quark perm (s);
    save_perm_q.reset();
    save_perm_q.bind(1,perm);
    save_perm_q.bind(2,group);
    save_perm_q.exec();
    count ++;
  }

  LOG4CXX_INFO(logger, "Migrated " << count << " groups permissions "
               << "in " << timer.get_seconds_elapsed() << "s.");

  std::string filename (data_io.get_group_permissions_filename());
  std::remove(filename.data());
}

void DataImpl ::migrate_group_descriptions(DataIO const &data_io) {
  LineReader * in (data_io.read_group_descriptions ());

  if (in == nullptr) {
    return;
  }

  std::string filename (data_io.get_group_descriptions_filename());

  LOG4CXX_INFO(logger,  "Migrating group descriptions from " << filename << "..." );

  StringView s, group;

  std::stringstream save_st;
  save_st << "with alias (gid) as (select id from `group` where name = ? ) "
          << "insert into `group_description` (group_id, description) select "
             "gid,? from alias ";
  SQLite::Statement save_desc_q(pan_db, save_st.str());

  while (in && !in->fail() && in->getline(group)) {
    // save in DB only if description has information
    if (group.pop_last_token (s, ':') && !s.empty() && s!="?" && s != group) {
      save_desc_q.reset();
      save_desc_q.bind(1, group);
      save_desc_q.bind(2, s);
      save_desc_q.exec();
    }
  }

  delete in;

  // remove obsolete file
  std::remove(filename.data());
  LOG4CXX_INFO(logger, "Migration of group descriptions done.");
}

void DataImpl ::migrate_group_xovers(DataIO const &data_io)
{
  LineReader * in (data_io.read_group_xovers ());
  if (in && !in->fail())
  {
    LOG4CXX_INFO(logger, "Migrating newsgroup.xov file to DB");
    StringView line;
    StringView groupname, total, unread;
    StringView xover, servername, low;

    SQLite::Statement insert_group_q(pan_db, R"SQL(
      insert into `group` (name) values (?) on conflict do nothing;
    )SQL");

    SQLite::Statement check_server_q(pan_db, R"SQL(
      select count() from `server` where pan_id = ?
    )SQL");

    SQLite::Statement count_q(pan_db, R"SQL(
      update `group` set total_article_count = ? , unread_article_count = ?
      where name = ?
    )SQL");

    SQLite::Statement insert_server_group_q(pan_db, R"SQL(
      insert into `server_group` (server_id, group_id) values (
        (select id from server where pan_id = ?),
        (select id from `group` where name = ?)
      ) on conflict do nothing;
    )SQL");

    SQLite::Statement xover_q(pan_db, R"SQL(
      update `server_group` set xover_high = ?
      where server_id = (select id from server where pan_id = ?)
        and group_id = (select id from `group` where name = ?)
    )SQL");


    int count ;
    // walk through the groups line-by-line...
    while (in->getline (line))
    {
      // skip comments and blank lines
      line.trim();
      if (line.empty() || *line.str=='#')
        continue;

      if (line.pop_token(groupname) && line.pop_token(total) && line.pop_token(unread))
      {
        SQLite::Transaction store_xov_transaction(pan_db);

        // store new group if needed (have no servername, so just create the group)
        insert_group_q.reset();
        insert_group_q.bind(1, groupname);
        insert_group_q.exec();

        // for each xref
        while (line.pop_token (xover)) {
          if (xover.pop_token(servername,':')) {
            // check if server is known (xov file may contain entries for removed servers, sic)
            check_server_q.reset();
            check_server_q.bind(1, servername);
            int s_count(0);
            while (check_server_q.executeStep()) {
              s_count = check_server_q.getColumn(0);
            }
            if (s_count == 1) {
              // store the server group link if needed
              insert_server_group_q.reset();
              insert_server_group_q.bind(1,servername);
              insert_server_group_q.bind(2,groupname);
              insert_server_group_q.exec();

              // store the unread_article_count and xover_high
              xover_q.reset();
              xover_q.bind(1,xover);
              xover_q.bind(2,servername);
              xover_q.bind(3,groupname);
              xover_q.exec();
            }
          }
        }

        // then update total and unread in group
        count_q.reset();
        count_q.bind(1,total);
        count_q.bind(2,unread);
        count_q.bind(3,groupname);
        count += count_q.exec();

        store_xov_transaction.commit();
      }
    }
    LOG4CXX_INFO(logger, "Migrated " << count << " records from newsgroup.xov into DB" );

    std::remove(data_io.get_group_xovers_filename().c_str());
  }
}

void DataImpl ::load_group_xovers_from_db() {
  TimeElapsed timer;
  LOG4CXX_DEBUG(logger,"Loading xovers data from DB");

  StringView groupname, total, unread, xover, server_pan_id;

  std::stringstream xover_st;
  xover_st << "select name, pan_id, xover_high "
           << "from `group` as g "
           << "join `server_group` as sg, `server` as s "
           << "where g.id == group_id and s.id == server_id and total_article_count is not null and g.pseudo == False;";
  SQLite::Statement xover_q(pan_db, xover_st.str());
  int count(0) ;

  // walk through the results row by row...
  while (xover_q.executeStep()) {
    count ++;
    groupname = xover_q.getColumn(0).getText();
    ReadGroup& g (_read_groups[groupname]);

    if (!xover_q.getColumn(2).isNull()) {
      server_pan_id = xover_q.getColumn(1).getText();
      xover = xover_q.getColumn(2).getText();
      g[server_pan_id]._xover_high = Article_Number(xover);
    }
  }

  LOG4CXX_INFO(logger, "Loaded " << count << " group xover info from DB in "
               << timer.get_seconds_elapsed() << "s.");
}

namespace
{
  typedef std::map < pan::Quark, std::string > quark_to_symbol_t;

  struct QuarkToSymbol {
    quark_to_symbol_t const &_map;
    virtual ~QuarkToSymbol () {
    }

    QuarkToSymbol(quark_to_symbol_t const &map) :
      _map(map)
    {
    }

    virtual std::string operator()(Quark const &quark) const
    {
      quark_to_symbol_t::const_iterator it (_map.find (quark));
      return it!=_map.end() ? it->second : quark.to_string();
    }
  };
}

void
DataImpl :: save_group_xovers ()
{
  if (_unit_test)
    return;

  SQLite::Statement group_q(pan_db,R"SQL(
    select total_article_count, unread_article_count from `group` where name = ?
  )SQL");

  // find the set of groups that have had an xover
  typedef std::set<Quark, AlphabeticalQuarkOrdering> xgroups_t;
  xgroups_t xgroups;
  foreach_const (read_groups_t, _read_groups, git) {
    ReadGroup const &group(git->second);
    group_q.reset();
    group_q.bind(1,git->first.c_str());
    bool is_xgroup;
    while (group_q.executeStep()) {
      is_xgroup = group_q.getColumn(1).getInt();
    }
    if (!is_xgroup)
      foreach_const (ReadGroup::servers_t, group._servers, sit)
        if ((is_xgroup = (static_cast<uint64_t>(sit->second._xover_high)!=0)))
          break;
    if (is_xgroup)
      xgroups.insert (git->first);
  }

  std::stringstream xover_st;
  xover_st << "update `server_group` set xover_high = ? "
           << "where server_id = (select id from server where pan_id = ?) "
           << "  and group_id = (select id from `group` where name = ?);";
  SQLite::Statement xover_q(pan_db, xover_st.str());

  // foreach xgroup
  foreach_const (xgroups_t, xgroups, it)
  {
    const Quark groupname (*it);
    ReadGroup const &g(*find_read_group(groupname));

    SQLite::Transaction store_xov_transaction(pan_db);

    foreach_const (ReadGroup::servers_t, g._servers, i) {
      if (static_cast<uint64_t>(i->second._xover_high) != 0) {
        xover_q.reset();
        xover_q.bind(1,static_cast<int64_t>(i->second._xover_high));
        xover_q.bind(2,i->first.c_str());
        xover_q.bind(3,groupname.c_str());
        int exec_count = xover_q.exec();
        if (exec_count != 1) {
          LOG4CXX_WARN(logger, "Unkwown group " << groupname << " or server pan_id " << i->first.c_str() << " when saving xover data in DB");
        };
      }
    }

    store_xov_transaction.commit();
  }
}

/****
*****
****/

Article_Number DataImpl ::get_xover_high(Quark const &groupname,
                                         Quark const &servername) const
{
  Article_Number high (0ul);
  ReadGroup::Server const *rgs(find_read_group_server(groupname, servername));
  if (rgs)
    high = rgs->_xover_high;
  return high;
}

void DataImpl ::set_xover_high(Quark const &group,
                               Quark const &server,
                               const Article_Number high)
{
  //std::cerr << LINE_ID << "setting " << get_server_address(server) << ':' << group << " xover high to " << high << std::endl;
  ReadGroup::Server& rgs (_read_groups[group][server]);
  rgs._xover_high = high;
}

void DataImpl ::add_groups(Quark const &server,
                           NewGroup const *newgroups,
                           size_t count)
{
  Server * s (find_server (server));
  assert (s != nullptr);

  {
    AlphabeticalQuarkOrdering o;
    Server::groups_t groups;
    std::vector<Quark> tmp;

    // make a groups_t from the added groups,
    // and merge it with the server's list of groups
    groups.get_container().reserve (count);
    for (NewGroup const *it = newgroups, *end = newgroups + count; it != end;
         ++it)
    {
      groups.get_container().push_back(it->group);
    }
    groups.sort ();
    std::set_union (s->groups.begin(), s->groups.end(),
                    groups.begin(), groups.end(),
                    std::back_inserter (tmp), o);
    tmp.erase (std::unique(tmp.begin(), tmp.end()), tmp.end());
    s->groups.get_container().swap (tmp);

    // make a groups_t of groups we didn't already have,
    // and merge it with _unsubscribed (i.e., groups we haven't seen before become unsubscribed)
    groups.clear ();
    tmp.clear ();
  }

  save_new_groups_in_db(server, newgroups, count);

  fire_grouplist_rebuilt ();

}

void DataImpl ::mark_group_read(Quark const &groupname)
{
  SQLite::Statement set_read_q(pan_db, R"SQL(
    update `article` as a set is_read = True
      where a.id in (
         select article_id
         from `article_group` as ag
         join `group` as g on g.id == ag.group_id
         where g.name = ?
    ) and is_read == False
  )SQL");

  set_read_q.bind(1, groupname.c_str());
  int count = set_read_q.exec();

  LOG4CXX_TRACE(logger, "Set " << count <<
                " articles as read for group " << groupname.c_str());

  ReadGroup * rg (find_read_group (groupname));
  if (rg != nullptr) {
    foreach (ReadGroup::servers_t, rg->_servers, it) {
      //std::cerr << LINE_ID << " marking read range [0..." << it->second._xover_high << "] in " << get_server_address(it->first) << ']' << std::endl;
      it->second._read.mark_range (static_cast<Article_Number>(0), it->second._xover_high, true);
    }
    save_group_xovers ();
    fire_group_read (groupname);
  }
}

void DataImpl ::set_group_subscribed(Quark const &group, bool subscribed)
{
  SQLite::Statement set_subscribed_q(pan_db, R"SQL(
    update `group` set subscribed = ? where name = ?
  )SQL");
  set_subscribed_q.bind(1, subscribed);
  set_subscribed_q.bind(2, group.c_str());
  int res = set_subscribed_q.exec();
  assert(res ==1);

  fire_group_subscribe (group, subscribed);
}

const std::string DataImpl ::get_group_description(Quark const &group) const
{
  std::stringstream st;
  st << "select (description) from `group_description` as gd join `group` as g "
     << "where g.name = ? and g.id = gd.group_id " ;
  SQLite::Statement desc_q(pan_db, st.str());
  desc_q.bind(1,group.c_str());

  std::string res;
  while (desc_q.executeStep()) {
    res.assign(desc_q.getColumn(0).getText());
  }
  return res;
}

// save group description if new or different from old description.
// Returns the number of affected rows
void DataImpl ::save_group_descriptions_in_db(NewGroup const *newgroups, int count)
{
  SQLite::Statement group_q(pan_db, "select id from `group` where name = ?" );

  SQLite::Statement desc_q(pan_db, R"SQL(
    insert into `group_description` (group_id, description)
      --  add new row with description
      values(?, @desc)
      -- clobber old description if different
      on conflict (group_id) do update set description = @desc where description != @desc ;
  )SQL" );

  int desc_count = 0;
  // keep any descriptions worth keeping that we don't already have...
  for (NewGroup const *it = newgroups, *end = newgroups + count; it != end;
       ++it)
  {
    NewGroup const &ng(*it);

    group_q.reset();
    group_q.bind(1, ng.group.c_str());
    int group_id(0);
    while(group_q.executeStep()) {
      group_id = group_q.getColumn(0);
    }

    // abort if group is unknown
    assert(group_id != 0);

    if (!ng.description.empty() && ng.description!="?" && ng.description != ng.group.c_str()) {
      desc_q.reset();
      desc_q.bind(1,group_id);
      desc_q.bind(2,ng.description);
      desc_count += desc_q.exec();
    }
  }

  LOG4CXX_DEBUG(logger, "saved " << desc_count << " group descriptions in DB ");
}

void DataImpl ::get_group_counts(Quark const &groupname,
                                 Article_Count &unread_count,
                                 Article_Count &article_count) const
{
  TimeElapsed timer;

  SQLite::Statement article_count_q(pan_db, R"SQL(
    select count() from `article_group` as ag
    join `group` as g on g.id == ag.group_id
      where g.name = ?
  )SQL");

  SQLite::Statement unread_count_q(pan_db, R"SQL(
    select count() from `article` as a
    join `article_group` as ag on a.id == ag.article_id
    join `group` as g on g.id == ag.group_id
      where g.name = ? and is_read = False
  )SQL");

  article_count_q.bind(1, groupname.c_str());
  while (article_count_q.executeStep())
    article_count = Article_Count(article_count_q.getColumn(0).getInt64());

  unread_count_q.bind(1, groupname.c_str());
  while (unread_count_q.executeStep())
    unread_count = Article_Count(unread_count_q.getColumn(0).getInt64());

  LOG4CXX_TRACE(logger, "Counted " << article_count << " articles (" << unread_count
                << " unread) in group " << groupname.c_str()
                << " in " << timer.get_seconds_elapsed() << "s.");
}

char DataImpl ::get_group_permission(Quark const &group) const
{
  SQLite::Statement query(pan_db, "select permission from `group` where name = ?");
  query.bind(group.c_str());

  char const *perm = nullptr;
  while (query.executeStep()) {
    perm = query.getColumn(0).getText();
  }
  assert(perm != nullptr);
  LOG4CXX_TRACE(logger, "got permission " << *perm << " for group " << group.c_str());
  // return only the first character
  return perm[0];
}

void DataImpl ::group_get_servers(Quark const &groupname, quarks_t &addme) const
{
  foreach_const (servers_t, _servers, it)
    if (it->second.groups.count (groupname))
      addme.insert (it->first);
}

void DataImpl ::server_get_groups(Quark const &servername,
                                  quarks_t &addme) const
{
  Server const *server(find_server(servername));
  if (server)
    addme.insert (server->groups.begin(), server->groups.end());
}

void
DataImpl :: get_subscribed_groups (std::vector<Quark>& setme) const
{
  TimeElapsed timer;
  SQLite::Statement q(pan_db, R"SQL(
    select name from `group` where subscribed = True and pseudo == False
  )SQL");

  while(q.executeStep()) {
    setme.push_back(Quark(q.getColumn(0).getText()));
  }

  LOG4CXX_TRACE(logger, "Found " << setme.size() << " subscribed groups in " <<
                timer.get_seconds_elapsed() << "s.");
}

void
DataImpl :: get_other_groups (std::vector<Quark>& setme) const
{
  TimeElapsed timer;
  SQLite::Statement q(pan_db, R"SQL(
    select name from `group` where subscribed = False and pseudo == False
  )SQL");

  while(q.executeStep()) {
    setme.push_back(Quark(q.getColumn(0).getText()));
  }

  LOG4CXX_TRACE(logger, "Found " << setme.size() << " unsubscribed groups in " <<
                timer.get_seconds_elapsed() << "s.");
}
