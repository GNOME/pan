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
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/macros.h>
#include "filter-info.h"

using namespace pan;

/***
****
***/

/*
 * Copy-and-swap idiom according to
 * http://stackoverflow.com/questions/3279543/what-is-the-copy-and-swap-idiom
 */

FilterInfo :: FilterInfo (const FilterInfo &that)
  : _type(that._type)
  , _ge(that._ge)
  , _header(that._header)
  , _text(that._text)
  , _negate(that._negate)
  , _needs_body(that._needs_body)
{
  foreach_const (aggregatesp_t, that._aggregates, it) {
    _aggregates.push_back (new FilterInfo(**it));
  }
}

void
swap (FilterInfo &first, FilterInfo &second)
{
  using std::swap;

  swap (first._type,       second._type);
  swap (first._ge,         second._ge);
  swap (first._header,     second._header);
  swap (first._text,       second._text);
  swap (first._aggregates, second._aggregates);
  swap (first._negate,     second._negate);
  swap (first._needs_body, second._needs_body);
}

FilterInfo &FilterInfo::operator = (FilterInfo other)
{
  swap (*this, other);

  return *this;
}

FilterInfo :: ~FilterInfo ()
{
  foreach (aggregatesp_t, _aggregates, it) {
    delete *it;
  }
}

void
FilterInfo :: clear ()
{
  _type = FilterInfo::TYPE_ERR;
  _ge = 0;
  _header.clear ();
  _text.clear ();
  foreach (aggregatesp_t, _aggregates, it) {
    delete *it;
  }
  _aggregates.clear ();
  _negate = false;
  _needs_body = false;
}

void
FilterInfo :: set_type_is (Type type) {
   clear ();
   _type = type;
}
void
FilterInfo :: set_type_ge (Type type, unsigned long ge) {
  clear ();
  _type = type;
  _ge = ge;
}

void
FilterInfo :: set_type_le (Type type, unsigned long le) {
  clear ();
  _type = type;
  _negate = true;
  _ge = le+1;  // le N == !ge N+1
}

void
FilterInfo :: set_type_aggregate_and () {
   clear ();
   _type = AGGREGATE_AND;
}
void
FilterInfo :: set_type_aggregate_or () {
   clear ();
   _type = AGGREGATE_OR;
}
void
FilterInfo :: set_type_text (const Quark                   & header,
                             const TextMatch::Description  & text)
{
  static const Quark subject("Subject"), from("From"),
    xref("Xref"), references("References"), newsgroups("Newsgroups"),
    message_Id("Message-Id"), message_ID("Message-ID");

  clear ();
  _type = TEXT;
  _header = header;
  _text.set (text);

  if( !(header == subject || header == from || header == message_Id ||
      header == message_ID || header ==  newsgroups || header == references ||
      header ==  xref) )
    _needs_body = true;
}

/****
*****
****/

void
FilterInfo :: set_type_binary ()
{
   set_type_is (IS_BINARY);
}
void
FilterInfo :: set_type_byte_count_ge (unsigned long ge)
{
   set_type_ge (BYTE_COUNT_GE, ge);
}
void
FilterInfo :: set_type_cached ()
{
   set_type_is (IS_CACHED);
}
void
FilterInfo :: set_type_crosspost_count_ge (unsigned long ge)
{
   set_type_ge (CROSSPOST_COUNT_GE, ge);
}
void
FilterInfo :: set_type_days_old_ge (unsigned long ge)
{
   set_type_ge (DAYS_OLD_GE, ge);
}
void
FilterInfo :: set_type_days_old_le (unsigned long le)
{
   set_type_le (DAYS_OLD_GE, le);
}
void
FilterInfo :: set_type_line_count_ge (unsigned long ge)
{
   set_type_ge (LINE_COUNT_GE, ge);
}
void
FilterInfo :: set_type_score_ge (unsigned long ge)
{
   set_type_ge (SCORE_GE, ge);
}
void
FilterInfo :: set_type_score_le (unsigned long le)
{
   set_type_le (SCORE_GE, le);
}
void
FilterInfo :: set_type_is_read ()
{
   set_type_is (IS_READ);
}
void
FilterInfo :: set_type_is_unread ()
{
   set_type_is (IS_UNREAD);
}
void
FilterInfo :: set_type_posted_by_me ()
{
   set_type_is (IS_POSTED_BY_ME);
}

std::string
FilterInfo :: describe () const
{
  std::string ret;
  char buf[4096];

  if (_type==IS_BINARY && _negate)
  {
    ret = _("article doesn't have attachments");
  }
  else if (_type==IS_BINARY)
  {
    ret = _("the article has attachments");
  }
  else if (_type==IS_CACHED && _negate)
  {
    ret = _("the article isn't cached locally");
  }
  else if (_type==IS_CACHED)
  {
    ret = _("the article is cached locally");
  }
  else if (_type==IS_POSTED_BY_ME && _negate)
  {
    ret = _("the article wasn't posted by you");
  }
  else if (_type==IS_POSTED_BY_ME)
  {
    ret = _("the article was posted by you");
  }
  else if (_type==IS_READ)
  {
    ret = _("the article has been read");
  }
  else if (_type==IS_UNREAD)
  {
    ret = _("the article hasn't been read");
  }
  else if (_type==BYTE_COUNT_GE && _negate)
  {
    g_snprintf (buf, sizeof(buf), _("the article is less than %ld bytes long"), _ge);
    ret = buf;
  }
  else if (_type==BYTE_COUNT_GE)
  {
    g_snprintf (buf, sizeof(buf), _("the article is at least %ld bytes long"), _ge);
    ret = buf;
  }
  else if (_type==LINE_COUNT_GE && _negate)
  {
    g_snprintf (buf, sizeof(buf), _("the article is less than %ld lines long"), _ge);
    ret = buf;
  }
  else if (_type==LINE_COUNT_GE)
  {
    g_snprintf (buf, sizeof(buf), _("the article is at least %ld lines long"), _ge);
    ret = buf;
  }
  else if (_type==DAYS_OLD_GE && _negate)
  {
    g_snprintf (buf, sizeof(buf), _("the article is less than %ld days old"), _ge);
    ret = buf;
  }
  else if (_type==DAYS_OLD_GE)
  {
    g_snprintf (buf, sizeof(buf), _("the article is at least %ld days old"), _ge);
    ret = buf;
  }
  else if (_type==CROSSPOST_COUNT_GE && _negate)
  {
    g_snprintf (buf, sizeof(buf), _("the article was posted to less than %ld groups"), _ge);
    ret = buf;
  }
  else if (_type==CROSSPOST_COUNT_GE)
  {
    g_snprintf (buf, sizeof(buf), _("the article was posted to at least %ld groups"), _ge);
    ret = buf;
  }
  else if (_type==SCORE_GE && _negate)
  {
    g_snprintf (buf, sizeof(buf), _("the article's score is less than %ld"), _ge);
    ret = buf;
  }
  else if (_type==SCORE_GE)
  {
    g_snprintf (buf, sizeof(buf), _("the article's score is %ld or higher"), _ge);
    ret = buf;
  }
  else if (_type==TEXT && _negate)
  {
#if 0
    const char * h (_header.c_str());
    const char * t (_text.get_state().text.c_str());
    switch (_text.get_state().type) {
      case TextMatch::CONTAINS:    g_snprintf (buf, sizeof(buf), _("%s doesn't contain \"%s\""), h, t); break;
      case TextMatch::IS:          g_snprintf (buf, sizeof(buf), _("%s isn't \"%s\""), h, t); break;
      case TextMatch::BEGINS_WITH: g_snprintf (buf, sizeof(buf), _("%s doesn't begin with \"%s\""), h, t); break;
      case TextMatch::ENDS_WITH:   g_snprintf (buf, sizeof(buf), _("%s doesn't end with \"%s\""), h, t); break;
      case TextMatch::REGEX:       g_snprintf (buf, sizeof(buf), _("%s doesn't match the regex \"%s\""), h, t); break;
    }
#else
    const char * h (_header.c_str());
    const char * t (_text._impl_text.c_str());
    switch (_text._impl_type) {
      case TextMatch::CONTAINS:    g_snprintf (buf, sizeof(buf), _("%s doesn't contain \"%s\""), h, t); break;
      case TextMatch::IS:          g_snprintf (buf, sizeof(buf), _("%s isn't \"%s\""), h, t); break;
      case TextMatch::BEGINS_WITH: g_snprintf (buf, sizeof(buf), _("%s doesn't begin with \"%s\""), h, t); break;
      case TextMatch::ENDS_WITH:   g_snprintf (buf, sizeof(buf), _("%s doesn't end with \"%s\""), h, t); break;
      case TextMatch::REGEX:       g_snprintf (buf, sizeof(buf), _("%s doesn't match the regex \"%s\""), h, t); break;
    }
#endif
    ret = buf;
  }
  else if (_type==TEXT)
  {
    const char * h (_header.c_str());
    //const char * t (_text.get_state().text.c_str());
    //switch (_text.get_state().type) {
    const char * t (_text._impl_text.c_str());
    switch (_text._impl_type) {
      case TextMatch::CONTAINS:    g_snprintf (buf, sizeof(buf), _("%s contains \"%s\""), h, t); break;
      case TextMatch::IS:          g_snprintf (buf, sizeof(buf), _("%s is \"%s\""), h, t); break;
      case TextMatch::BEGINS_WITH: g_snprintf (buf, sizeof(buf), _("%s begins with \"%s\""), h, t); break;
      case TextMatch::ENDS_WITH:   g_snprintf (buf, sizeof(buf), _("%s ends with \"%s\""), h, t); break;
      case TextMatch::REGEX:       g_snprintf (buf, sizeof(buf), _("%s matches the regex \"%s\""), h, t); break;
   }
    ret = buf;
  }
  else if (_type==AGGREGATE_AND && _negate)
  {
    ret = _("Any of these tests fail:");
    ret += "\n";
    foreach_const (aggregatesp_t, _aggregates, it)
      ret += "   " + (*it)->describe() + "\n";
  }
  else if (_type==AGGREGATE_AND)
  {
    ret = _("All of these tests pass:");
    ret += "\n";
    foreach_const (aggregatesp_t, _aggregates, it)
      ret += "   " + (*it)->describe() + "\n";
  }
  else if (_type==AGGREGATE_OR && _negate)
  {
    ret = _("None of these tests pass:");
    ret += "\n";
    foreach_const (aggregatesp_t, _aggregates, it)
      ret += "   " + (*it)->describe() + "\n";
  }
  else if (_type==AGGREGATE_OR)
  {
    ret = _("Any of these tests pass:");
    ret += "\n";
    foreach_const (aggregatesp_t, _aggregates, it)
      ret += "   " + (*it)->describe() + "\n";
  }

  return ret;
}
