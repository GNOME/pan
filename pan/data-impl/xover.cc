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
#include <cmath>
#include <fstream>
#include <glib/gi18n.h>
#include <gmime/gmime.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/data/article.h>
#include "data-impl.h"

using namespace pan;

namespace
{
  bool parse_multipart_subject (const StringView   & subj,
                                int                & part,
                                int                & parts,
                                std::string        & no_part)
  {
    const char * numerator = nullptr;
    const char * denominator = nullptr;

    const char * s (subj.begin());
    const char * pch (subj.end());
    while (pch != s)
    {
      // find the ']' of [n/N]
      --pch;
      if ((pch[1]!=')' && pch[1]!=']') || !isdigit(*pch))
        continue;

      // find the '/' of [n/N]
      while (s!=pch && isdigit(*pch))
        --pch;
      if (s==pch || (*pch!='/' && *pch!='|'))
        continue;

      // N -> parts
      denominator = pch+1;
      --pch;

      // find the '[' of [n/N]
      while (s!=pch && isdigit(*pch))
        --pch;
      if (s==pch || (*pch!='(' && *pch!='[')) {
        denominator = nullptr;
        continue;
      }

      // N -> part
      numerator = pch+1;
      char * numerator_end (nullptr);
      part = (int) strtol (numerator, &numerator_end, 10);
      parts = atoi (denominator);

      if (part > parts) {
        // false positive...
        numerator = denominator = nullptr;
        part = parts = 0;
        continue;
      }

      no_part.assign (subj.str, numerator-subj.str);
      no_part.append (numerator_end, (subj.str+subj.len)-numerator_end);
      return true;
    }

    return false;
  }

  void find_parts (const StringView   & subj,
                   const Quark        & group,
                   int                  line_count,
                   int                & part,
                   int                & parts,
                   std::string        & norm)
  {
    if (!parse_multipart_subject (subj, part, parts, norm))
    {
      part = parts = 0;
      norm.assign (subj.str, subj.len);
    }

    /* if not a multipart yet, AND if it's a big message, AND
       it's either in one of the pictures/fan/sex groups or it
       has commonly-used image names in the subject, it's probably
       a single-part binary */
    if (!parts && line_count>400) {
      const StringView gname (group.c_str());
      if (((gname.strstr("binaries") ||
            gname.strstr("fan") ||
            gname.strstr("mag") ||
            gname.strstr("sex")))
           ||
          ((subj.strstr(".jpg") || subj.strstr(".JPG") ||
            subj.strstr(".jpeg") || subj.strstr(".JPEG") ||
            subj.strstr(".gif") || subj.strstr(".GIF") ||
            subj.strstr(".png") || subj.strstr(".PNG"))))
        part = parts = 1;
    }

    /* but if it's starting the subject with "Re:" and doesn't have
       many lines, it's probably a followup to a part, rather than an
       actual part. Note that server may provide a bogus zero line
       count in which case the last test is misleading. */
    if (Article::has_reply_leader(subj) && line_count != 0 && line_count < 100)
      part = parts = 0;

    /* Subjects containing (0/N) aren't part of an N-part binary;
       they're text description articles that accompany the binary. */
    if (part == 0)
      parts = 0;
  }
}

void
DataImpl :: xover_clear_workarea (const Quark& group)
{
   pan_debug ("Clearing the XOVER workearea for " << group);

   _xovers.erase (group);
   if (group == _cached_xover_group) {
      _cached_xover_group.clear ();
      _cached_xover_entry = nullptr;
   }
}

DataImpl :: XOverEntry&
DataImpl :: xover_get_workarea (const Quark& group)
{
   XOverEntry * entry (nullptr);
   if (group == _cached_xover_group)
      entry = _cached_xover_entry;
   else {
      _cached_xover_group = group;
      _cached_xover_entry = entry = &_xovers[group];
   }
   return *entry;
}

void
DataImpl :: xover_ref (const Quark& group)
{
  // sanity clause
  pan_return_if_fail (!group.empty());

  // ref the articles
  ref_group (group);

  // ref the xover
  XOverEntry& workarea (xover_get_workarea (group));
  ++workarea.refcount;

  // populate the normalized lookup for multipart detection...
  GroupHeaders * h (get_group_headers (group));
  foreach_const (nodes_t, h->_nodes, it) {
    const Quark& mid (it->first);
    const Article * a (it->second->_article);
    if (a != nullptr)
      workarea._subject_lookup.insert (std::pair<Quark,Quark>(a->subject,mid));
  }
}

void
DataImpl :: xover_flush (const Quark& group)
{
  XOverEntry& workarea (xover_get_workarea (group));

  on_articles_added (group, workarea._added_batch);
  workarea._added_batch.clear();
  on_articles_changed (group, workarea._changed_batch, true);
  workarea._changed_batch.clear();
  workarea._last_flush_time = time(nullptr);
}

void
DataImpl :: xover_unref (const Quark& group)
{
  XOverEntry& workarea (xover_get_workarea (group));
  if (!--workarea.refcount)
  {
    xover_flush (group);
    xover_clear_workarea (group);
  }

  unref_group (group);
}


void
DataImpl :: set_xover_low (const Quark   & group,
                           const Quark   & server,
                           const Article_Number   low)
{
  ReadGroup::Server * rgs (find_read_group_server (group, server));
  if (rgs != nullptr)
    rgs->_read.mark_range (static_cast<Article_Number>(0), low, true);
}

const Article*
DataImpl :: xover_add (const Quark         & server,
                       const Quark         & group,
                       const StringView    & subject,
                       const StringView    & author,
                       const time_t          time_posted,
                       const StringView    & message_id,
                       const StringView    & references_in,
                       const unsigned long   byte_count,
                       const unsigned long   line_count,
                       const StringView    & xref,
                       const bool            is_virtual)
{
  if (is_virtual)
    ref_group(group);

  GroupHeaders * h (get_group_headers (group));
  if (!h && !is_virtual) {
    Log::add_err_va (_("Error reading from %s: unknown group \"%s\""),
                     get_server_address(server).c_str(),
                     group.c_str());
    return nullptr;
  }


//  std::cerr<<"xover add : "<<subject<<" "<<author<<" "<<message_id<<" lines "<<line_count<<" bytes "<<byte_count<<std::endl;

  const Article* new_article (nullptr);

  XOverEntry& workarea (xover_get_workarea (group));
  const std::string references (
    GNKSA :: remove_broken_message_ids_from_references (references_in));

  /***
  **** Multipart Handling
  ***/

  h->_dirty = true;

  int part_index, part_count;
  std::string multipart_subject;
  find_parts (subject, group, line_count, part_index, part_count, multipart_subject);
  const Quark multipart_subject_quark (multipart_subject);
  Quark art_mid;

  if (part_count > 1)
  {
    // walk through the articles we've already got for the group
    // to see if there's already an Article allocated to this
    // multipart.  If there is, we use it here instead of adding a new one

    typedef XOverEntry::subject_to_mid_t::const_iterator cit;
    const std::pair<cit,cit> range (workarea._subject_lookup.equal_range (multipart_subject_quark));
    for (cit it(range.first), end(range.second); it!=end && art_mid.empty(); ++it) {
      const Quark& candidate_mid (it->second);
      const Article* candidate (h->find_article (candidate_mid));
      if (candidate
          && (candidate->author == author)
          && ((int)candidate->get_total_part_count() == part_count))
        art_mid = candidate_mid;
    }
  }


  if (art_mid.empty())
  {
    art_mid = message_id;

    if (part_count > 1)
      workarea._subject_lookup.insert(std::pair<Quark,Quark>(multipart_subject_quark, art_mid));

    // if we don't already have this article...
    if (!h->find_article (art_mid))
    {
      //std::cerr << LINE_ID << " We didn't have this article yet, so creating an instance..." << std::endl;
      Article& a (h->alloc_new_article());
      a.author = author;
      a.subject = multipart_subject_quark;
      a.message_id = art_mid;
      a.is_binary = part_count >= 1;
      a.set_part_count (a.is_binary ? part_count : 1);
      a.time_posted = time_posted;
      a.xref.insert (server, xref);
      load_article (group, &a, references);
      new_article = &a;

      workarea._added_batch.insert (art_mid);
    }
  }

  /**
  ***  Add the article's part info
  **/

  {
    const int number (part_count<2 ? 1 : part_index);
    load_part (group, art_mid,
               number, message_id,
               line_count, byte_count);
  }

  if (!workarea._added_batch.count(art_mid))
    workarea._changed_batch.insert(art_mid);

  // maybe flush the batched changes
  if ((time(nullptr) - workarea._last_flush_time) >= 10)
    xover_flush (group);

  if (is_virtual)
      unref_group(group);

  return new_article;
}
