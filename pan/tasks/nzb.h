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

#ifndef __NZB_H__
#define __NZB_H__

#include <iosfwd>
#include <vector>
#include <pan/data/data.h>

namespace pan
{
  class StringView;
  class ArticleCache;
  class Task;

  /**
   * Converts NZB files in to a vector of TaskArticle objects, and vice versa.
   * @ingroup tasks
   */
  struct NZB
  {
    static void tasks_from_nzb_string (const StringView     & nzb,
                                       const StringView     & save_path,
                                       ArticleCache         & cache,
                                       EncodeCache          & encode_cache,
                                       ArticleRead          & read,
                                       const ServerRank     & ranks,
                                       const GroupServer    & gs,
                                       std::vector<Task*>   & appendme);

    static void tasks_from_nzb_file (const StringView     & filename,
                                     const StringView     & save_path,
                                     ArticleCache         & cache,
                                     EncodeCache          & encode_cache,
                                     ArticleRead          & read,
                                     const ServerRank     & ranks,
                                     const GroupServer    & gs,
                                     std::vector<Task*>   & appendme);

    static std::ostream&  nzb_to_xml (std::ostream             & out,
                                      const std::vector<Task*> & tasks);

    static std::ostream&  upload_list_to_xml_file (std::ostream& out,
                                                   const std::vector<Article*> & tasks);

    static std::ostream&  nzb_to_xml_file (std::ostream             & out,
                                           const std::vector<Task*> & tasks);

    static std::ostream &print_header(std::ostream &out);

    static std::ostream &print_footer(std::ostream &out);

  };
}

#endif
