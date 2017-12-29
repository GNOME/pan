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

#ifndef __DataIO_h__
#define __DataIO_h__

#include <iosfwd>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/general/line-reader.h>

namespace pan
{
  /**
   * Specifies the datafiles used by DataImpl.
   * It's abstracted out so that unit tests can substitute in its own data.
   * This private class should only be used by code in the data-impl module.
   *
   * @ingroup data_impl
   */
  struct DataIO
  {
    DataIO() {}
    virtual ~DataIO() {}

    virtual std::string get_scorefile_name () const;
    virtual std::string get_posting_name () const;
    virtual std::string get_server_filename () const;

    virtual void clear_group_headers (const Quark& group);

    virtual LineReader* read_tasks () const;
    virtual LineReader* read_group_xovers () const;
    virtual LineReader* read_group_headers (const Quark& group) const;
    virtual LineReader* read_group_descriptions () const;
    virtual LineReader* read_group_permissions () const;
    virtual LineReader* read_download_stats () const;

    virtual std::ostream* write_tasks ();
    virtual std::ostream* write_server_properties ();
    virtual std::ostream* write_group_xovers ();
    virtual std::ostream* write_group_descriptions ();
    virtual std::ostream* write_group_permissions ();
    virtual std::ostream* write_download_stats ();
    virtual std::ostream* write_group_headers (const Quark& group);
    virtual void write_done (std::ostream*);

    virtual LineReader* read_file (const StringView& filename) const;
    virtual std::ostream* write_file (const StringView& filename);
  };
}

#endif
