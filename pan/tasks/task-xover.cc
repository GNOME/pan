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

#include <config.h>
#include <cassert>
#include <cerrno>

extern "C" {
#include <stdio.h>
}
#define PROTOTYPES
#include <uulib/uudeview.h>
#include <glib/gi18n.h>
#include <gmime/gmime-utils.h>

#include <fstream>
#include <iostream>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/data/data.h>
#include "nntp.h"
#include "task-xover.h"

using namespace pan;

namespace
{
  std::string
  get_short_name(const StringView& in)
  {
    static const StringView moderated("moderated");
    static const StringView d("d");

    StringView myline, long_token;

    // find the long token -- use the last, unless that's "moderated" or "d"
    myline = in;
    myline.pop_last_token(long_token, '.');
    if (!myline.empty() && (long_token == moderated || long_token == d))
      myline.pop_last_token(long_token, '.');

    // build a new string where each token is shortened except for long_token
    std::string out;
    myline = in;
    StringView tok;
    while (myline.pop_token(tok, '.'))
      {
        out.insert(out.end(), tok.begin(),
            (tok == long_token ? tok.end() : tok.begin() + 1));
        out += '.';
      }
    if (!out.empty())
      out.erase(out.size() - 1);

    return out;
  }

  std::string
  get_description(const Quark& group, TaskXOver::Mode mode)
  {
    char buf[1024];
    if (mode == TaskXOver::ALL)
      snprintf(buf, sizeof(buf), _("Getting all headers for \"%s\""),
          group.c_str());
    else if (mode == TaskXOver::NEW)
      snprintf(buf, sizeof(buf), _("Getting new headers for \"%s\""),
          group.c_str());
    else
      // SAMPLE
      snprintf(buf, sizeof(buf), _("Sampling headers for \"%s\""),
          group.c_str());
    return std::string(buf);
  }
}

TaskXOver::TaskXOver(Data & data, const Quark & group, Mode mode, unsigned long sample_size) :
  Task("XOVER", get_description(group, mode)),
  _data(data),
  _group(group),
  _short_group_name(get_short_name(StringView(group.c_str()))),
  _mode(mode),
  _sample_size(sample_size),
  _days_cutoff(mode == DAYS ? (time(nullptr) - (sample_size * 24 * 60 * 60)) : 0),
  _group_xover_is_reffed(false),
  _bytes_so_far(0),
  _parts_so_far(0ul),
  _articles_so_far(0ul),
  _total_minitasks(0)
{

  pan_debug("ctor for " << group);

  // add a ``GROUP'' MiniTask for each server that has this group
  // initialize the _high lookup table to boundaries
  quarks_t servers;
  _data.group_get_servers(group, servers);
  foreach_const (quarks_t, servers, it)if (_data.get_server_limits(*it))
    {
      Data::Server* s (_data.find_server(*it));
      const MiniTask group_minitask (MiniTask::GROUP);
      _server_to_minitasks[*it].push_front (group_minitask);
      _high[*it] = data.get_xover_high (group, *it);
    }
  init_steps(0);

  // tell the users what we're up to
  set_status(group.c_str());

  update_work();
}

TaskXOver::~TaskXOver()
{
  if (_group_xover_is_reffed)
    {
      foreach (server_to_high_t, _high, it)
        {
          _data.set_xover_high (_group, it->first, it->second);
        }
      _data.xover_unref (_group);
    }
  _data.fire_group_entered(_group, static_cast<Article_Count>(1), static_cast<Article_Count>(0));
}

void
TaskXOver::use_nntp(NNTP* nntp)
{

  const Quark& server(nntp->_server);
  CompressionType comp;
  _data.get_server_compression_type(server, comp);

  pan_debug("got an nntp from " << nntp->_server);

  // if this is the first nntp we've gotten, ref the xover data
  if (!_group_xover_is_reffed)
    {
      _group_xover_is_reffed = true;
      _data.xover_ref(_group);
    }

  MiniTasks_t& minitasks(_server_to_minitasks[server]);
  if (minitasks.empty())
    {
      pan_debug(
          "That's interesting, I got a socket for " << server << " but have no use for it!");
      _state._servers.erase(server);
      check_in(nntp, OK);
    }
  else
    {
      const MiniTask mt(minitasks.front());
      minitasks.pop_front();
      switch (mt._type)
        {
      case MiniTask::GROUP:
        pan_debug("GROUP " << _group << " command to " << server);
        nntp->group(_group, this);
        break;
      case MiniTask::XOVER:
        pan_debug("XOVER " << mt._low << '-' << mt._high << " to " << server);
        _last_xover_number[nntp] = mt._low;
        if (comp == HEADER_COMPRESS_XZVER || comp == HEADER_COMPRESS_DIABLO)
          nntp->xzver(_group, mt._low, mt._high, this);
        else
          nntp->xover(_group, mt._low, mt._high, this);
        break;
      default:
        assert(0);
        }
      update_work();
    }
}

/***
 ****
 ***/

void
TaskXOver::on_nntp_group(NNTP * nntp, const Quark & group, Article_Count qty,
    Article_Number low, Article_Number high)
{
  const Quark& servername(nntp->_server);
  CompressionType comp;
  _data.get_server_compression_type(servername, comp);
  const bool compression_enabled(comp != HEADER_COMPRESS_NONE);

  // new connections can tickle this...
  if (_servers_that_got_xover_minitasks.count(servername))
    return;

  _servers_that_got_xover_minitasks.insert(servername);

  pan_debug(
      "got GROUP result from " << nntp->_server << " (" << nntp << "): " << " qty " << qty << " low " << low << " high " << high);

  Article_Number l(low), h(high);
  _data.set_xover_low(group, nntp->_server, low);
  //std::cerr << LINE_ID << " This group's range is [" << low << "..." << high << ']' << std::endl;

  if (_mode == ALL || _mode == DAYS)
    l = low;
  else if (_mode == SAMPLE)
    {
      _sample_size = std::min(_sample_size, static_cast<uint64_t>(high - low));
      //std::cerr << LINE_ID << " and I want to sample " <<  _sample_size << " messages..." << std::endl;
      l = std::max(low, high + 1 - _sample_size);
    }
  else
    { // NEW
      Article_Number xh(_data.get_xover_high(group, nntp->_server));
      //std::cerr << LINE_ID << " current xover high is " << xh << std::endl;
      l = std::max(xh + 1, low);
    }

  if (l <= high)
    {
      //std::cerr << LINE_ID << " okay, I'll try to get articles in [" << l << "..." << h << ']' << std::endl;
      add_steps(static_cast<uint64_t>(h - l));
      const int INCREMENT(compression_enabled ? 10000 : 1000);
      MiniTasks_t& minitasks(_server_to_minitasks[servername]);
      //Unfortunately we need to push everything to the front of the list, so
      //that we process all the xovers before we process the next group.
      //But we want to fetch all the articles in order so if someone exits,
      //on resumption we can resume from where we left off. Therefore, we build
      //a list of things to do in reverse order
      std::vector<MiniTask> tasks;
      if (_mode != DAYS)
      {
        tasks.reserve(static_cast<uint64_t>(h - l));
      }
      for (Article_Number m = l; m <= h; m += INCREMENT)
        {
          //A note: It may not be necessary to cap the high here, the spec isn't
          //terribly clear about what happens if new articles come into
          //existence on the server while it is working out the response to the
          //xover. So be safe.
          const MiniTask mt(MiniTask::XOVER, m, std::min(h, m + INCREMENT));
          pan_debug(
              "adding MiniTask for " << servername << ": xover [" << mt._low << '-' << mt._high << "]");
          if (_mode == DAYS)
          {
            minitasks.push_front(mt);
            ++_total_minitasks;
          }
          else
          {
            tasks.insert(tasks.begin(), mt);
          }
        }

      //And this reverses them again, so we're back in the right order. We don't
      //do it for days as there's a cutoff in the receiving code that stops us
      //as soon as we get older than the specified number of days. However, for
      //fetching anything else, we go forward so that it's possible to carry on
      //fetching new articles without leaving a gap after exit or crash.
      if (_mode != DAYS)
      {
        for (auto const & mt : tasks)
        {
          minitasks.push_front(mt);
          ++_total_minitasks;
        }
      }
    }
  else
    {
      //std::cerr << LINE_ID << " nothing new here..." << std::endl;
      _high[nntp->_server] = high;
    }
}

namespace
{
  unsigned long
  view_to_ul(const StringView& view)
  {
    unsigned long ul = 0ul;

    if (!view.empty())
      {
        errno = 0;
        ul = strtoul(view.str, nullptr, 10);
        if (errno)
          ul = 0ul;
      }

    return ul;
  }

  bool
  header_is_nonencoded_utf8(const StringView& in)
  {
    const bool is_nonencoded(!in.strstr("=?"));
    const bool is_utf8(g_utf8_validate(in.str, in.len, nullptr));
    return is_nonencoded && is_utf8;
  }
}

void
TaskXOver::on_nntp_line(NNTP * nntp, const StringView & line)
{

  const Quark& server(nntp->_server);
  CompressionType comp;
  _data.get_server_compression_type(server, comp);

  if (comp != HEADER_COMPRESS_NONE)
  {
    int sock_id = nntp->_socket->get_id();
    if (_streams.count(sock_id) == 0)
      _streams[sock_id] = new std::stringstream();
    *_streams[sock_id] << line;
    // \r\n was stripped, append it again because ydecode needs it
    if (comp == HEADER_COMPRESS_XZVER || comp == HEADER_COMPRESS_DIABLO) *_streams[sock_id] <<"\r\n";
  }
  else
    on_nntp_line_process(nntp, line);
}

void
TaskXOver::on_nntp_line_process(NNTP * nntp, const StringView & line)
{

  pan_return_if_fail(nntp != nullptr);
  pan_return_if_fail(!nntp->_server.empty());
  pan_return_if_fail(!nntp->_group.empty());

  _bytes_so_far += line.len;

  unsigned int lines = 0u;
  unsigned long bytes = 0ul;
  Article_Number number(0);
  StringView subj, author, date, mid, tmp, xref, l(line);
  std::string ref;
  bool ok = !l.empty();
  ok = ok && l.pop_token(tmp, '\t');
  if (ok)
    number = Article_Number(tmp);
  tmp.clear();
  ok = ok && l.pop_token(subj, '\t');
  if (ok)
    subj.trim();
  ok = ok && l.pop_token(author, '\t');
  if (ok)
    author.trim();
  ok = ok && l.pop_token(date, '\t');
  if (ok)
    date.trim();
  ok = ok && l.pop_token(mid, '\t');
  if (ok)
    mid.trim();

  //handle multiple "References:"-message-ids correctly. (hack for some faulty servers)
  ok = ok && l.pop_token(tmp, '\t');
  do
    {
      // usenetbucket uses a (null) (sic!) value for an empty reference list. hence the following hack
      if (tmp.empty() || tmp == "(null)" || tmp == "null")
        continue;
      if (tmp.front() == '<')
        {
          tmp.trim();
          ref += tmp;
          tmp.clear();
        }
      else
        break;
    }
  while ((ok = ok && l.pop_token(tmp, '\t')));
  if (ok)
    bytes = view_to_ul(tmp);
  tmp.clear();
  ok = ok && l.pop_token(tmp, '\t');
  if (ok)
    lines = view_to_ul(tmp);
  ok = ok && l.pop_token(xref, '\t');
  if (ok)
    xref.trim();

  if (xref.len > 6 && !strncmp(xref.str, "Xref: ", 6))
    {
      xref = xref.substr(xref.str + 6, nullptr);
      xref.trim();
    }

  // is this header corrupt?
  if (static_cast<uint64_t>(number) == 0 // missing number
      || subj.empty() // missing subject
      || author.empty() // missing author
      || date.empty() // missing date
      || mid.empty() // missing mid
      || mid.front() != '<') // corrupt mid
	/// Concerning bug : https://bugzilla.gnome.org/show_bug.cgi?id=650042
	/// Even if we didn't get a proper reference here, continue.
	//|| (!ref.empty() && ref.front()!='<'))
		return;

	// if news server doesn't provide an xref, fake one
	char * buf(nullptr);
	if (xref.empty())
		xref = buf = g_strdup_printf("%s %s:%" G_GUINT64_FORMAT,
				nntp->_server.c_str(), nntp->_group.c_str(), static_cast<uint64_t>(number));

	Article_Number& h(_high[nntp->_server]);
	h = std::max(h, number);

	const char * fallback_charset = NULL; // FIXME

	// are we done?
	GDateTime * time_posted_gd = g_mime_utils_header_decode_date(date.str);
	const time_t time_posted = g_date_time_to_unix(time_posted_gd);
	if (_mode == DAYS && time_posted < _days_cutoff) {
		_server_to_minitasks[nntp->_server].clear();
		return;
	}

	++_parts_so_far;

	const Article * article = _data.xover_add(nntp->_server, nntp->_group,
			(header_is_nonencoded_utf8(subj) ?
					subj : header_to_utf8(subj, fallback_charset).c_str()),
			(header_is_nonencoded_utf8(author) ?
					author : header_to_utf8(author, fallback_charset).c_str()),
			time_posted, mid, StringView(ref), bytes, lines, xref);

	if (article)
		++_articles_so_far;

	// emit a status update
	Article_Number& prev = _last_xover_number[nntp];
	increment_step(static_cast<uint64_t>(number) - static_cast<uint64_t>(prev));
	prev = number;
	if (!(_parts_so_far % 500))
		set_status_va(_("%s (%lu parts, %lu articles)"),
				_short_group_name.c_str(), _parts_so_far, _articles_so_far);

	// cleanup
	g_free(buf);
}

void
TaskXOver::on_nntp_done(NNTP * nntp, Health health, const StringView & response)
{

  const Quark& servername(nntp->_server);
  CompressionType comp;
  _data.get_server_compression_type(servername, comp);
  const bool compression_enabled(comp == HEADER_COMPRESS_XZVER || comp == HEADER_COMPRESS_DIABLO || comp == HEADER_COMPRESS_XFEATURE);
  bool fail (false);

  if (health == OK && compression_enabled && atoi(response.str) == 0)
  {
    std::stringstream* buffer(nullptr);
    std::stringstream out, out2;
    if (comp == HEADER_COMPRESS_XZVER || comp == HEADER_COMPRESS_DIABLO)
    {
      buffer = _streams[nntp->_socket->get_id()];
      if (compression::ydecode(buffer, &out))
        fail = !compression::inflate_zlib(&out, &out2, comp);
      else
        fail = true;
    }
    if (comp == HEADER_COMPRESS_XFEATURE)
    {
        buffer = _streams[nntp->_socket->get_id()];
        fail = !buffer ? true : !compression::inflate_zlib(buffer, &out2, comp);
    }
    if (buffer) buffer->clear();

    if (!fail)
    {
      char buf[4096];
      while (true)
      {
        std::istream& str = out2.getline(buf, sizeof(buf));
        if (str.fail() || str.bad() || str.eof()) break;
        on_nntp_line_process(nntp, buf);
      }
    }
  }
  update_work(true);
  check_in(nntp, fail ? ERR_LOCAL : health);
}

void
TaskXOver::update_work(bool subtract_one_from_nntp_count)
{
  int nntp_count(get_nntp_count());
  if (subtract_one_from_nntp_count)
    --nntp_count;

  // find any servers we still need
  quarks_t servers;
  foreach_const (server_to_minitasks_t, _server_to_minitasks, it)if (!it->second.empty())
  servers.insert (it->first);

  //std::cerr << LINE_ID << " servers: " << servers.size() << " nntp: " << nntp_count << std::endl;

  if (!servers.empty())
    _state.set_need_nntp(servers);
  else if (nntp_count)
    _state.set_working();
  else
    {
      _state.set_completed();
      set_finished(OK);
      char str[4096];
      g_snprintf(str, sizeof(str), _("Getting new headers for \"%s\" done."), _group.c_str());
      verbose (str);
    }
}

unsigned long
TaskXOver::get_bytes_remaining() const
{
  unsigned int minitasks_left(0);
  foreach_const (server_to_minitasks_t, _server_to_minitasks, it)minitasks_left += it->second.size();

  const double percent_done(
      _total_minitasks ?
          (1.0 - minitasks_left / (double) _total_minitasks) : 0.0);
  if (percent_done < 0.1) // impossible to estimate
    return 0;
  const unsigned long total_bytes = (unsigned long) (_bytes_so_far
      / percent_done);
  return total_bytes - _bytes_so_far;
}
