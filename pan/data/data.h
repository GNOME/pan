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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __Data_h__
#define __Data_h__

#include <string>
#include <list>
#include <map>
#include <vector>

#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/scorefile.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/server-info.h>

namespace pan
{
  class FilterInfo;

  /**
   * Data Interface class for seeing the mapping between groups and servers.
   * @ingroup data
   */
  struct GroupServer {
    virtual ~GroupServer () {}
    virtual void group_get_servers (const Quark& group, quarks_t&) const=0;
    virtual void server_get_groups (const Quark& server, quarks_t&) const=0;
  };

  struct ArticleReferences {
    virtual ~ArticleReferences () {}
    virtual void get_article_references (const Quark& group, const Article*, std::string& setme) const = 0;
  };

  /**
   * Data Interface class for querying and setting an articles' Read state.
   *
   * Judging what's 'read' can be problematic if we're sharing a
   * newsrc file with other newsreaders: if we have server A and B,
   * and the user reads an article on server A with a different client,
   * then A and B's newsrc will disagree on whether the article is read.
   * Since the user _has_ read the article (on A with the other client),
   * Pan considers the article read even though B doesn't know about it.
   *
   * Also, a read article should change back to unread if it changes
   * from an incomplete multipart to a complete multipart as new
   * parts are added to it. 
   *
   * @ingroup data
   */
  struct ArticleRead {
    virtual ~ArticleRead () {}
    virtual bool is_read (const Article* a) const=0;
    virtual void mark_read (const Article&, bool read=true)=0;
    virtual void mark_read (const Article** articles, unsigned long article_count, bool read=true)=0;
  };


  /**
   * Data Interface class for a posting profile: email address, signature file, extra headers, etc.
   *
   * @ingroup data
   */
  struct Profile
  {
    std::string username;
    std::string address;
    bool use_sigfile;
    enum { TEXT, FILE, COMMAND };
    int sig_type;
    std::string signature_file;
    std::string attribution;
    std::string fqdn;
    Quark posting_server;

    void get_from_header (std::string& s) const {
      s = "\"" + username + "\" <" + address + ">";
    }
    typedef std::map<std::string,std::string> headers_t;
    headers_t headers;
    const std::string& get_header(const char * key) const {
      static const std::string nil;
      headers_t::const_iterator it (headers.find(key));
      return it==headers.end() ? nil : it->second;
    }

    Profile(): use_sigfile(false), sig_type(TEXT) {}

    void clear() { username.clear(); address.clear();
                   use_sigfile = false;
                   sig_type = TEXT;
                   signature_file.clear(); attribution.clear(); }
  };

  /**
   * Data Interface class for managing posting profiles.
   * @see Profile
   *
   * @ingroup data
   */
  class Profiles
  {
    public:
      virtual ~Profiles () {}

    public:
      typedef std::vector<std::string> strings_t;
      virtual void get_editors (strings_t& setme) const = 0;
      virtual const std::string& get_active_editor () const = 0;

    public:
      virtual std::set<std::string> get_profile_names () const = 0;
      virtual bool has_profiles () const = 0;
      virtual bool has_from_header (const StringView& from) const = 0;
      virtual bool get_profile (const std::string& profile_name, Profile& setme) const = 0;

    public:
      virtual void delete_profile (const std::string& profile_name) = 0;
      virtual void add_profile (const std::string& profile_name, const Profile& profile) = 0;
      virtual void set_editors (const strings_t&) = 0;
      virtual void set_active_editor (const StringView&) = 0;
      
    protected:
      Profiles () {}
  };

  /**
   * The main interface class for Pan's data backend.
   *
   * It's intended to decouple the higher tasks/gui code from the backend so
   * that we can swap in a database backend or otherwise tinker with the
   * implementation structures without having ripple effects into the rest
   * of Pan.
   *
   * @ingroup data
   * FIXME: doesn't address manual deletion of servers, groups
   * FIXME: doesn't address folders
   * FIXME: renaming a server is so onerous I'm inclined to not implement it.
   */
  class Data:
    public virtual ServerInfo,
    public virtual GroupServer,
    public virtual ArticleRead,
    public virtual Profiles,
    public virtual ArticleReferences
  {
    protected:

      Data () {}
      virtual ~Data () {}

    public:

      virtual void save_state () = 0;

    public:

      virtual ArticleCache& get_cache () = 0; 

      virtual const ArticleCache& get_cache () const = 0; 

    public:

      virtual quarks_t get_servers () const = 0;

      virtual void delete_server (const Quark& server) = 0;

      virtual Quark add_new_server () = 0;

      virtual void set_server_auth (const Quark       & server,
                                    const StringView  & username,
                                    const StringView  & password) = 0;

      virtual void set_server_addr (const Quark       & server,
                                    const StringView  & address,
                                    const int           port) = 0;

      virtual void set_server_limits (const Quark     & server,
                                      int               max_connections) = 0;

      virtual bool get_server_addr (const Quark   & server,
                                    std::string   & setme_address,
                                    int           & setme_port) const = 0;

      virtual bool get_server_auth (const Quark   & server,
                                    std::string   & setme_username,
                                    std::string   & setme_password) const = 0;

      virtual int get_server_limits (const Quark & server) const = 0;

    /*****************************************************************
    ***
    ***  OBSERVERS
    **/

    public:

      /**
       * Interface class for objects that listen to a Data's events.
       * @ingroup data
       */
      struct Listener
      {
        Listener () {}
        virtual ~Listener () {}

        virtual void on_grouplist_rebuilt () {}
        virtual void on_group_read (const Quark& group) {}
        virtual void on_group_subscribe (const Quark& group, bool sub) {}
        virtual void on_group_counts (const Quark& group,
                                      unsigned long unread,
                                      unsigned long total) {}
      };

      void add_listener (Listener * l);

      void remove_listener (Listener * l);

    protected:

      void fire_grouplist_rebuilt ();

      void fire_group_read (const Quark&);

      void fire_group_counts (const Quark&,
                              unsigned long unread,
                              unsigned long total);

      void fire_group_subscribe (const Quark&, bool);

    private:

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

    /*****************************************************************
    ***
    ***  GROUPS
    **/

    public: // mutators

      /** Struct to be passed to add_groups() when adding new newsgroups. */
      struct NewGroup
      {
        /** from LIST */
        Quark group;
        /** from LIST NEWSGROUPS*/
        std::string description;
        /** y, n, or m for posting, no posting, moderated */
        char permission;

        NewGroup(): permission('?') {}
      };

      /**
       * To be called after an NNTP GROUPLIST task.
       * This will call listeners' on_grouplist_rebuilt().
       */
      virtual void add_groups (const Quark      & servername,
                               const NewGroup   * new_groups,
                               size_t             new_group_count) = 0;

      /**
       * This will call listeners' on_group_counts().
       */
      virtual void mark_group_read  (const Quark & group)=0;

      virtual void set_group_subscribed (const Quark & group,
                                         bool          sub)=0;

    public: // accessors

      virtual const std::string& get_group_description (const Quark&group) const=0;

      /**
       * Get an alphabetically-sorted list of subscribed newsgroups.
       */
      virtual void get_subscribed_groups (std::vector<Quark>&) const = 0;

      /**
       * Get an alphabetically-sorted list of non-subscribed newsgroups.
       */
      virtual void get_other_groups (std::vector<Quark>&) const = 0;

      /**
       * Get the number of articles and unread articles that we
       * currently have headers on-hand in the specified group.
       * from previous xover sessions.
       */
      virtual void get_group_counts (const Quark   & group,
                                     unsigned long & setme_unread,
                                     unsigned long & setme_total) const=0;

      virtual char get_group_permission (const Quark & group) const=0;

      //virtual void group_get_servers (const Quark& group, quarks_t&) const=0;




    /*****************************************************************
    ***
    ***  HEADERS
    **/

    public:

      enum ShowType
      {
        SHOW_ARTICLES,
        SHOW_THREADS,
        SHOW_SUBTHREADS
      };

      /**
       * A snapshot of Group's header info.
       * Trees should be deleted by the client when no longer needed.
       *
       * Article pointers retrieved from this struct are invalidated
       * by deleting the Tree.
       */
      class ArticleTree
      {
        public:

          virtual ~ArticleTree () { };

        /*************************************************************
        ***
        ***  EVENT NOTIFICATION
        **/

        public:

          /**
           * When new articles come in from a server, or old articles
           * are aged off, or when the user deletes articles, these
           * changes are reflected in the Tree.
           *
           * In the case of new articles, the Tree's existing filter
           * is applied to them and new articles that survive the filter
           * are threaded into the tree. 
           *
           * In the case of deleted articles, their children are
           * reparented to the youngest surviving ancestor and then the
           * Article objects are deleted.
           *
           * A Diffs object summarizes these changes to make it easier
           * for clients to synchronize their Views to the changes.
           *
           * @see Listener
           * @see addListener()
           * @see removeListener()
           * @ingroup data
           */
          struct Diffs
          {
            /**
             * A pair of quarks giving the message-id of the new article and its parent.
             * If the new article has is a root node, parent will be empty().
             * @ingroup data
             */
            struct Added
            {
              Quark message_id;
              Quark parent;
            };

            typedef std::map<Quark,Added> added_t;
            added_t added;

            /**
             * A tuple of quarks giving the message-id of the article that was
             * reparented, the message-id of its old parent, and the message-id
             * of its new parent.
             *
             * If the reparented article is new, old_parent may be empty().
             * If the reparented article has become a root node, new_parent will be empty().
             *
             * @ingroup data
             */
            struct Reparent
            {
              Quark message_id;
              Quark old_parent;
              Quark new_parent;
            };

            typedef std::map<Quark,Reparent> reparented_t;
            reparented_t reparented;

            quarks_t removed;

            quarks_t changed;
          };

          /**
           * Interface class for objects that listen to an ArticleTree's events.
           * @ingroup data
           * @see ArticleTree
           */
          struct Listener
          {
            virtual ~Listener () {}
            virtual void on_tree_change (const Diffs&) = 0;
          };

        private:

          typedef std::set<Listener*> listeners_t;
          listeners_t _listeners;

        public:

          virtual void add_listener (Listener * l) { _listeners.insert(l); }

          virtual void remove_listener (Listener * l) { _listeners.erase(l); }

        protected:

          /** the quirky way of incrementing 'it' is to prevent it from being
              invalidated if on_tree_change() calls remove_listener() */
          void fire_diffs (const Diffs& diffs) const {
            listeners_t::iterator it, e;
            for (it=_listeners.begin(), e=_listeners.end(); it!=e; )
              (*it++)->on_tree_change (diffs);
          }
              

        /*************************************************************
        ***
        ***  ACCESSORS
        **/

        public:

          typedef std::vector<const Article*> articles_t;

          /**
           * if message_id is empty, the root nodes are returned.
           */
          virtual void get_children (const Quark  & mid,
                                     articles_t   & setme) const = 0;

          virtual size_t size () const = 0;

          virtual const Article* get_parent (const Quark& mid) const=0;

          virtual const Article* get_article (const Quark& mid) const=0;

          virtual void set_filter (const ShowType     show_type = SHOW_ARTICLES,
                                   const FilterInfo * filter_or_null_to_reset = 0) = 0;
      };

       /**
        * Get a collection of headers that match the specified filter.
        */
       virtual ArticleTree* group_get_articles (const Quark       & group,
                                                const ShowType      show_type = SHOW_ARTICLES,
                                                const FilterInfo  * criteria = 0) const=0;

       virtual void group_clear_articles (const Quark& group) = 0;

       typedef std::set<const Article*> unique_articles_t;

       virtual void delete_articles             (const unique_articles_t&) = 0;

       virtual void get_article_scores (const Quark& group, const Article&, Scorefile::items_t& setme) const = 0;

       virtual void add_score (const StringView           & section_wildmat,
                               int                          score_value,
                               bool                         score_assign_flag,
                               int                          lifespan_days,
                               bool                         all_items_must_be_true,
                               const Scorefile::AddItem   * items,
                               size_t                       item_count,
                               bool                         do_rescore) = 0;

       virtual void comment_out_scorefile_line (const StringView    & filename,
                                                size_t                begin_line,
                                                size_t                end_line,
                                                bool                  do_rescore) = 0;

        virtual void rescore () = 0;



    /*****************************************************************
    ***
    ***  HEADERS - XOVER
    **/

    public:

      /**
       * The first call to xover_ref() for a group can indicate to Data
       * to set up internal scaffolding it needs for the xover_add()
       * calls that are about to come flooding in.
       *
       * This is ref/unref rather than begin/end so that multiple
       * connections can be used during an xref session.
       *
       * @see get_last_xover_time()
       * @see xover_unref ()
       */
      virtual void xover_ref (const Quark& group) = 0;

      /**
       * A new header to add to a group.
       * This must be called inside an xover_ref() / xover_unref() block.
       *
       * If a new article was created -- as opposed to a new part
       * being added to an existing article -- then a pointer to the
       * new article is returned.
       *
       * FIXME: this return value is kind of odd, and is just there
       * to suit task-xover's needs.
       */
      virtual const Article* xover_add  (const Quark          & server,
                                         const Quark          & group,
                                         const StringView     & subject,
                                         const StringView     & author,
                                         const StringView     & time,
                                         const StringView     & message_id,
                                         const StringView     & references,
                                         const unsigned long    byte_count,
                                         const unsigned long    line_count,
                                         const StringView     & xref) = 0;

      /**
       * The last call to xover_unref() for a group can indicate to Data
       * that it's safe to tear down any internal scaffolding set up in
       * xover_ref().
       */
      virtual void xover_unref  (const Quark      & group) = 0;

      /**
       * Returns the high number of the most recent XOVER command
       * run on the specified {server,group}, or 0 if it's never 
       * been run there.
       */
      virtual unsigned long get_xover_high (const Quark  & group,
                                    const Quark  & server) const = 0;

       /**
        * After an XOVER command has been run, its range should be set
        * here so that get_xover_high() will work properly.
        *
        * @see get_xover_high()
        */
       virtual void set_xover_high (const Quark         & group,
                                    const Quark         & server,
                                    const unsigned long    high) = 0;

       /**
        * Used to fold the Newsrc ranges together:
        * articles below the low group number can all be
        * pruned from the newsrc string.
        */
       virtual void set_xover_low (const Quark         & group,
                                   const Quark         & server,
                                   const unsigned long   low) = 0;

  };
}

#endif
