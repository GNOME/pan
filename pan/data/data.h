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

#ifndef __Data_h__
#define __Data_h__

#include <list>
#include <map>
#include <string>
#include <vector>

#include <pan/data/article-cache.h>
#include <pan/data/article.h>
#include <pan/data/encode-cache.h>
#include <pan/data/server-info.h>
#include <pan/general/compression.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/scorefile.h>

#include <pan/gui/prefs.h>
#include <pan/gui/progress-view.h>

#ifdef HAVE_GKR
#endif

namespace pan {
class FilterInfo;
class RulesInfo;
class Queue;
class CertStore;

/**
 * Data Interface class for seeing the mapping between groups and servers.
 * @ingroup data
 */
struct GroupServer
{
    virtual ~GroupServer()
    {
    }

    virtual void group_get_servers(Quark const &group, quarks_t &) const = 0;
    virtual void server_get_groups(Quark const &server, quarks_t &) const = 0;
};

struct ArticleReferences
{
    virtual ~ArticleReferences()
    {
    }

    virtual void get_article_references(Quark const &group,
                                        Article const *,
                                        std::string &setme) const = 0;
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
struct ArticleRead
{
    virtual ~ArticleRead()
    {
    }

    virtual bool is_read(Article const *a) const = 0;
    virtual void mark_read(Article const &, bool read = true) = 0;
    virtual void mark_read(Article const **articles,
                           unsigned long article_count,
                           bool read = true) = 0;
};

/**
 * Data Interface class for a posting profile: email address, signature file,
 * extra headers, etc.
 *
 * @ingroup data
 */
struct Profile
{
    std::string username;
    std::string address;
    bool use_sigfile;
    bool use_gpgsig;

    enum
    {
      TEXT,
      FILE,
      COMMAND,
      GPGSIG
    };

    int sig_type;
    std::string gpg_sig_uid;
    std::string signature_file;
    std::string attribution;
    std::string fqdn;
    std::string face;
    std::string xface;
    Quark posting_server;

    void get_from_header(std::string &s) const
    {
      s = "\"" + username + "\" <" + address + ">";
    }

    typedef std::map<std::string, std::string> headers_t;
    headers_t headers;

    std::string const &get_header(char const *key) const
    {
      static std::string const nil;
      headers_t::const_iterator it(headers.find(key));
      return it == headers.end() ? nil : it->second;
    }

    Profile() :
      use_sigfile(false),
      use_gpgsig(false),
      sig_type(TEXT)
    {
    }

    void clear()
    {
      username.clear();
      address.clear();
      use_sigfile = false;
      use_gpgsig = false;
      sig_type = TEXT;
      signature_file.clear();
      attribution.clear();
    }
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
    virtual ~Profiles()
    {
    }

  public:
    virtual std::set<std::string> get_profile_names() const = 0;
    virtual bool has_profiles() const = 0;
    virtual bool has_from_header(StringView const &from) const = 0;
    virtual bool get_profile(std::string const &profile_name,
                             Profile &setme) const = 0;

  public:
    virtual void delete_profile(std::string const &profile_name) = 0;
    virtual void add_profile(std::string const &profile_name,
                             Profile const &profile) = 0;

  protected:
    Profiles()
    {
    }
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
class Data :
  public virtual ServerInfo,
  public virtual GroupServer,
  public virtual ArticleRead,
  public virtual Profiles,
  public virtual ArticleReferences
{

  public:
    struct PasswordData
    {
        Quark server;
        StringView user;
        gchar *pw;
    };

  public:
    struct Server
    {
        std::string username;
        std::string password;
        std::string host;
        std::string newsrc_filename;
        std::string cert;
        int port;
        int article_expiration_age;
        int max_connections;
        int rank;
        int ssl_support;
        int trust;
        int compression_type;
        typedef sorted_vector<Quark, true, AlphabeticalQuarkOrdering> groups_t;
        groups_t groups;
        gchar *gkr_pw;

        Server() :
          port(STD_NNTP_PORT),
          article_expiration_age(31),
          max_connections(2),
          rank(1),
          ssl_support(0),
          trust(0),
          compression_type(0) /* NONE */,
          gkr_pw(NULL)
        {
        }
    };

  protected:
    Data()
    {
    }

    virtual ~Data()
    {
    }

  public:
    virtual void save_state() = 0;
    virtual Queue *get_queue() = 0;
    virtual Queue *get_queue() const = 0;
    virtual Prefs const &get_prefs() = 0;
    virtual Prefs const &get_prefs() const = 0;

  public:
    virtual ArticleCache &get_cache() = 0;
    virtual ArticleCache const &get_cache() const = 0;

    virtual EncodeCache &get_encode_cache() = 0;
    virtual EncodeCache const &get_encode_cache() const = 0;

    virtual CertStore &get_certstore() = 0;
    virtual CertStore const &get_certstore() const = 0;

  public:
#ifdef HAVE_GKR
    virtual gboolean password_encrypt(const PasswordData &) = 0;
    virtual gchar *password_decrypt(PasswordData &) const = 0;
#endif
    /** Gets a quark to the provided hostname */
    bool find_server_by_host_name(std::string const &server,
                                  Quark &setme) const override = 0;

    virtual Server const *find_server(Quark const &server) const = 0;

    virtual Server *find_server(Quark const &server) = 0;

    virtual quarks_t get_servers() const = 0;

    virtual void delete_server(Quark const &server) = 0;

    virtual Quark add_new_server() = 0;

    void set_server_auth(Quark const &server,
                         StringView const &username,
                         gchar *&password,
                         bool use_gkr) override = 0;

    void set_server_trust(Quark const &servername,
                          int const setme) override = 0;

    void set_server_addr(Quark const &server,
                         StringView const &address,
                         int const port) override = 0;

    void set_server_limits(Quark const &server,
                           int max_connections) override = 0;

    bool get_server_addr(Quark const &server,
                         std::string &setme_address,
                         int &setme_port) const override = 0;

    bool get_server_auth(Quark const &server,
                         std::string &setme_username,
                         gchar *&setme_password,
                         bool use_gkr) override = 0;

    bool get_server_trust(Quark const &servername, int &) const override = 0;

    bool get_server_compression_type(Quark const &servername,
                                     CompressionType &) const override = 0;

    std::string get_server_cert(Quark const &server) const override = 0;

    int get_server_limits(Quark const &server) const override = 0;

    /*****************************************************************
    ***
    ***  OBSERVERS
    **/

  public:
    typedef std::vector<Article const *> articles_t;

    /**
     * Interface class for objects that listen to a Data's events.
     * @ingroup data
     */
    struct Listener
    {
        Listener()
        {
        }

        virtual ~Listener()
        {
        }

        virtual void on_grouplist_rebuilt()
        {
        }

        virtual void on_group_read(Quark const &group UNUSED)
        {
        }

        virtual void on_group_subscribe(Quark const &group UNUSED,
                                        bool sub UNUSED)
        {
        }

        virtual void on_group_counts(Quark const &group UNUSED,
                                     Article_Count unread UNUSED,
                                     Article_Count total UNUSED)
        {
        }

        virtual void on_group_entered(Quark const &group UNUSED,
                                      Article_Count unread UNUSED,
                                      Article_Count total UNUSED)
        {
        }

        /* listener for article flag, don't call too often */
        virtual void on_article_flag_changed(articles_t &a UNUSED,
                                             Quark const &group UNUSED)
        {
        }
    };

    void add_listener(Listener *l);

    void remove_listener(Listener *l);

  protected:
    void fire_grouplist_rebuilt();

    void fire_group_read(Quark const &);

    void fire_group_counts(Quark const &,
                           Article_Count unread,
                           Article_Count total);

    void fire_group_subscribe(Quark const &, bool);

  private:
    typedef std::set<Listener *> listeners_t;
    listeners_t _listeners;

  public:
    virtual void fire_article_flag_changed(articles_t &a, Quark const &group);
    virtual void fire_group_entered(Quark const &group,
                                    Article_Count unread,
                                    Article_Count total);

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

        NewGroup() :
          permission('?')
        {
        }
    };

    /**
     * To be called after an NNTP GROUPLIST task.
     * This will call listeners' on_grouplist_rebuilt().
     */
    virtual void add_groups(Quark const &servername,
                            NewGroup const *new_groups,
                            size_t new_group_count) = 0;

    /**
     * This will call listeners' on_group_counts().
     */
    virtual void mark_group_read(Quark const &group) = 0;

    virtual void set_group_subscribed(Quark const &group, bool sub) = 0;

  public: // accessors
    virtual std::string const &get_group_description(
      Quark const &group) const = 0;

    /**
     * Get an alphabetically-sorted list of subscribed newsgroups.
     */
    virtual void get_subscribed_groups(std::vector<Quark> &) const = 0;

    /**
     * Get an alphabetically-sorted list of non-subscribed newsgroups.
     */
    virtual void get_other_groups(std::vector<Quark> &) const = 0;

    /**
     * Get the number of articles and unread articles that we
     * currently have headers on-hand in the specified group.
     * from previous xover sessions.
     */
    virtual void get_group_counts(Quark const &group,
                                  Article_Count &setme_unread,
                                  Article_Count &setme_total) const = 0;

    virtual char get_group_permission(Quark const &group) const = 0;

    // virtual void group_get_servers (const Quark& group, quarks_t&) const=0;

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
        virtual ~ArticleTree() {};

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
             * A pair of quarks giving the message-id of the new article and its
             * parent. If the new article has is a root node, parent will be
             * empty().
             * @ingroup data
             */
            struct Added
            {
                Quark message_id;
                Quark parent;
            };

            typedef std::map<Quark, Added> added_t;
            added_t added;

            /**
             * A tuple of quarks giving the message-id of the article that was
             * reparented, the message-id of its old parent, and the message-id
             * of its new parent.
             *
             * If the reparented article is new, old_parent may be empty().
             * If the reparented article has become a root node, new_parent will
             * be empty().
             *
             * @ingroup data
             */
            struct Reparent
            {
                Quark message_id;
                Quark old_parent;
                Quark new_parent;
            };

            typedef std::map<Quark, Reparent> reparented_t;
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
            virtual ~Listener()
            {
            }

            virtual void on_tree_change(Diffs const &) = 0;
        };

      private:
        typedef std::set<Listener *> listeners_t;
        listeners_t _listeners;

      public:
        virtual void add_listener(Listener *l)
        {
          _listeners.insert(l);
        }

        virtual void remove_listener(Listener *l)
        {
          _listeners.erase(l);
        }

      protected:
        /** the quirky way of incrementing 'it' is to prevent it from being
            invalidated if on_tree_change() calls remove_listener() */
        void fire_diffs(Diffs const &d) const
        {
          if (! d.added.empty() || ! d.removed.empty() || ! d.reparented.empty()
              || ! d.changed.empty())
          {
            listeners_t::iterator it, e;
            for (it = _listeners.begin(), e = _listeners.end(); it != e;)
            {
              (*it++)->on_tree_change(d);
            }
          }
        }

        /*************************************************************
        ***
        ***  ACCESSORS
        **/

      public:
        typedef std::vector<Article const *> articles_t;

        /**
         * if message_id is empty, the root nodes are returned.
         */
        virtual void get_children(Quark const &mid,
                                  articles_t &setme) const = 0;

        virtual size_t size() const = 0;

        virtual Article const *get_parent(Quark const &mid) const = 0;

        virtual Article const *get_article(Quark const &mid) const = 0;

        virtual void set_filter(
          ShowType const show_type = SHOW_ARTICLES,
          FilterInfo const *filter_or_null_to_reset = nullptr) = 0;

        virtual void set_rules(
          RulesInfo const *filter_or_null_to_reset = nullptr) = 0;
    };

    /**
     * Get a collection of headers that match the specified filter.
     */
    virtual ArticleTree *group_get_articles(
      Quark const &group,
      Quark const &save_path, // for auto-download
      ShowType const show_type = SHOW_ARTICLES,
      FilterInfo const *criteria = nullptr,
      RulesInfo const *rules = nullptr) const = 0;

    virtual void group_clear_articles(Quark const &group) = 0;

    typedef std::set<Article const *> unique_articles_t;

    virtual void delete_articles(unique_articles_t const &) = 0;

    virtual void get_article_scores(Quark const &group,
                                    Article const &,
                                    Scorefile::items_t &setme) const = 0;

    virtual void add_score(StringView const &section_wildmat,
                           int score_value,
                           bool score_assign_flag,
                           int lifespan_days,
                           bool all_items_must_be_true,
                           Scorefile::AddItem const *items,
                           size_t item_count,
                           bool do_rescore) = 0;

    virtual void comment_out_scorefile_line(StringView const &filename,
                                            size_t begin_line,
                                            size_t end_line,
                                            bool do_rescore) = 0;

    virtual void rescore_articles(Quark const &group, quarks_t const mids) = 0;

    virtual void rescore_group_articles(Quark const &group) = 0;

    virtual void rescore() = 0;

    /* Get the scorefile name.
     *
     * This is a little bit unclean but it allows people to find and edit
     * the scorefile until such point as a proper editor is written for
     * it
     */
    virtual std::string get_scorefile_name() const = 0;

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
    virtual void xover_ref(Quark const &group) = 0;

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
    virtual Article const *xover_add(Quark const &server,
                                     Quark const &group,
                                     StringView const &subject,
                                     StringView const &author,
                                     time_t const time,
                                     StringView const &message_id,
                                     StringView const &references,
                                     unsigned long const byte_count,
                                     unsigned long const line_count,
                                     StringView const &xref,
                                     bool const is_virtual = false) = 0;

    /**
     * The last call to xover_unref() for a group can indicate to Data
     * that it's safe to tear down any internal scaffolding set up in
     * xover_ref().
     */
    virtual void xover_unref(Quark const &group) = 0;

    /**
     * Returns the high number of the most recent XOVER command
     * run on the specified {server,group}, or 0 if it's never
     * been run there.
     */
    virtual Article_Number get_xover_high(Quark const &group,
                                          Quark const &server) const = 0;

    /**
     * After an XOVER command has been run, its range should be set
     * here so that get_xover_high() will work properly.
     *
     * @see get_xover_high()
     */
    virtual void set_xover_high(Quark const &group,
                                Quark const &server,
                                Article_Number const high) = 0;

    /**
     * Used to fold the Newsrc ranges together:
     * articles below the low group number can all be
     * pruned from the newsrc string.
     */
    virtual void set_xover_low(Quark const &group,
                               Quark const &server,
                               Article_Number const low) = 0;

    /** Sets the queue interface */
    virtual void set_queue(Queue *q) = 0;
};
} // namespace pan

#endif
