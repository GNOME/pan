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

#ifndef __Profiles_h__
#define __Profiles_h__

#include <map>
#include <set>
#include <string>
#include <vector>
#include <iosfwd>
#include <pan/general/string-view.h>
#include <pan/data/data.h>

namespace pan
{
  class DataIO;

  /**
   * This private class should only be used by classes in the same module.
   *
   * It's an implementation of the Profiles class.
   */
  class ProfilesImpl: public virtual Profiles
  {
    public:
      ProfilesImpl (DataIO& io);
      virtual ~ProfilesImpl ();

    public:
      virtual std::set<std::string> get_profile_names () const;
      virtual bool has_profiles () const;
      virtual bool has_from_header (const StringView& from) const;
      virtual bool get_profile (const std::string& profile_name, Profile& setme) const;

    public:
      virtual void delete_profile (const std::string& profile_name);
      virtual void add_profile (const std::string& profile_name, const Profile& profile);
      
    private:
      void clear ();
      void load (const StringView& filename);
      void serialize (std::ostream&) const;
      void save () const;

    private:
      typedef std::map<std::string,Profile> profiles_t;
      profiles_t profiles;
      std::string active_profile;
      DataIO& _data_io;
  };
}

#endif
