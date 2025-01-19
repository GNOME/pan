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

#ifndef __ProfilesDialog_h__
#define __ProfilesDialog_h__

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <pan/data/data.h>

namespace pan {
/**
 * Dialog for editing a posting profile.
 * @ingroup GUI
 */
class ProfileDialog
{
  public:
    ProfileDialog(Data const &,
                  StringView const &profile_name,
                  Profile const &profile,
                  GtkWindow *parent);
    ~ProfileDialog();

    GtkWidget *root()
    {
      return _root;
    }

    void get_profile(std::string &setme_name, Profile &setme_profile);

  private:
    GtkWidget *_root;
    GtkWidget *_name_entry;
    GtkWidget *_username_entry;
    GtkWidget *_address_entry;
    GtkWidget *_msgid_fqdn_entry;
    GtkWidget *_face_entry;
    GtkWidget *_xface_entry;
    GtkWidget *_attribution_entry;
    GtkWidget *_signature_file_check;
    GtkWidget *_server_combo;
    GtkWidget *_extra_headers_tv;
    GtkWidget *_signature_file_combo;

  public:
    GtkWidget *_signature_file_combo_box;
    GtkWidget *_signature_file;
    GtkWidget *_gpg_sig_entry;

    static bool run_until_valid_or_cancel(ProfileDialog &p);
};

/**
 * Dialog for managing all posting profiles.
 * @ingroup GUI
 */
class ProfilesDialog
{
  public:
    ProfilesDialog(Data const &data, Profiles &profiles, GtkWindow *parent);
    ~ProfilesDialog();

    GtkWidget *root()
    {
      return _root;
    }

  public:
    void edit_profile();
    void delete_profile();
    void create_new_profile();
    void rebuild_store();
    void refresh_buttons();

  private:
    Data const &_data;
    Profiles &_profiles;
    GtkWidget *_root;
    GtkWidget *_view;
    GtkWidget *_edit_button;
    GtkWidget *_remove_button;
    GtkListStore *_store;
};
} // namespace pan

#endif
