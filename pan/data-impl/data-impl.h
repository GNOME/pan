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

#ifndef __DataImpl_h__
#define __DataImpl_h__

#include <SQLiteCpp/Database.h>
#include <string>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>

#include "pan/data-impl/header-filter.h"
#include "pan/data-impl/header-rules.h"
#include <pan/data-impl/article-filter.h>
#include <pan/data-impl/data-io.h>
#include <pan/data-impl/memchunk.h>
#include <pan/data-impl/profiles.h>
#include <pan/data/article-cache.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/data/encode-cache.h>
#include <pan/general/macros.h>
#include <pan/general/map-vector.h>
#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <pan/general/prefs.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/blowfish.h>
#include <pan/usenet-utils/numbers.h>
#include <pan/usenet-utils/scorefile.h>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <pan/data/cert-store.h>
#endif

namespace pan {
typedef std::vector<Article const *> articles_t;
typedef Data::PasswordData PasswordData;

void load_db_schema_file(SQLiteDb &pan_db, char const *file);
void load_db_schema(SQLiteDb &pan_db);

/**
 * File-based implementation of the `Data' backend interface.
 *
 * Most of the files are stored in $PAN_HOME, which defaults to
 * $HOME/.pan2 if the PAN_HOME environmental variable isn't set.
 *
 * @ingroup data_impl
 */
class DataImpl final : public Data, public TaskArchive, public ProfilesImpl
{

    /**
     *** SERVERS
     **/

  public:
    /* The ProfilesImpl will own and destruct the DataIO object */
    DataImpl(StringView const &cache_ext,
             Prefs &prefs,
             bool unit_test = false,
             int cache_megs = 10,
             DataIO *source = new DataIO());
    virtual ~DataImpl();

  public:
    ArticleCache &get_cache() override
    {
      return _cache;
    }

    ArticleCache const &get_cache() const override
    {
      return _cache;
    }

    EncodeCache &get_encode_cache() override
    {
      return _encode_cache;
    }

    EncodeCache const &get_encode_cache() const override
    {
      return _encode_cache;
    }

    CertStore &get_certstore() override
    {
      return _certstore;
    }

    CertStore const &get_certstore() const override
    {
      return _certstore;
    }

    Prefs &get_prefs() override
    {
      return _prefs;
    }

    Prefs const &get_prefs() const override
    {
      return _prefs;
    }

    Queue *get_queue() override
    {
      return _queue;
    }

    Queue *get_queue() const override
    {
      return _queue;
    }

    void set_queue(Queue *q) override
    {
      _queue = q;
    }

  private:
    ArticleCache _cache;
    EncodeCache _encode_cache;
    CertStore _certstore;
    Queue *_queue;

public:
#ifdef HAVE_GKR
    gboolean password_encrypt(PasswordData const &) override;
    gchar *password_decrypt(PasswordData &) const override;
#endif
    void cleanup_db() const override;
  private:
    void rebuild_backend();
    void rebuild_server_data();
    void rebuild_group_data();
    void rebuild_group_xover_data();
    void rebuild_group_description_data();
    void rebuild_group_permission_data();
    bool const _unit_test;
    DataIO *_data_io;
    Prefs &_prefs;

    /**
    *** SERVERS
    **/

  private: // implementation
    void migrate_server_properties_into_db(DataIO const &);
    void save_server_in_db(std::string pan_id, Server *s, Prefs &prefs);

    typedef Loki::AssocVector<Quark, Server> servers_t;

    servers_t _servers;

  public:
    Server const *find_server(Quark const &server) const override;
    Server *find_server(Quark const &server) override;
    bool find_server_by_host_name(std::string const &server,
                                  Quark &setme) const override;
    Server *read_server(Quark const &pan_id) const;
    void read_server(Quark const &pan_id, Server *server) const;

  public: // mutators
    void delete_server(Quark const &server) override;
    void delete_server_from_db(std::string host);

    Quark add_new_server() override;

    void set_server_auth(Quark const &server,
                         StringView const &username,
                         gchar *&password,
                         bool use_gkr) override;

    void set_server_trust(Quark const &servername, int setme) override;

    void set_server_addr(Quark const &server,
                         StringView const &host,
                         int const port) override;

    void set_server_limits(Quark const &server, int max_connections) override;

    void set_server_rank(Quark const &server, int rank) override;

    void set_server_ssl_support(Quark const &server, int ssl) override;

    void set_server_cert(Quark const &server, StringView const &cert) override;

    void set_server_article_expiration_age(Quark const &server,
                                           int days) override;

    void set_server_compression_type(Quark const &server,
                                     int const setme) override;

    void save_server_info(Quark const &server) override;

  public: // accessors
    quarks_t get_servers() const override;
    quarks_t get_server_ids_from_db() const;

    bool get_server_auth(Quark const &server,
                         std::string &setme_username,
                         gchar *&setme_password,
                         bool use_gkr) override;
    void get_server_auth(Server *server,
                         std::string &setme_username,
                         gchar *&setme_password,
                         bool use_gkr);

    bool get_server_trust(Quark const &servername, int &) const override;

    bool get_server_compression_type(Quark const &servername,
                                     CompressionType &) const override;

    bool get_server_addr(Quark const &server,
                         std::string &setme_host,
                         int &setme_port) const override;

    std::string get_server_address(Quark const &servername) const override;

    bool get_server_ssl_support(Quark const &server) const override;

    std::string get_server_cert(Quark const &server) const override;

    int get_server_rank(Quark const &server) const override;

    int get_server_limits(Quark const &server) const override;

    int get_server_article_expiration_age(Quark const &server) const override;

    /**
    *** GROUPS
    **/

  private: // implementation
    typedef sorted_vector<Quark, true> unique_sorted_quarks_t;

    /**
     * Represents a newsgroup that's been read.
     *
     * Since most groups are never read, the `read' fields are separated
     * out into this structure, so that it can be instantiated on demand.
     * Since most news servers have tens of thousands of newsgroups,
     * this represents a big memory savings.
     *
     * This private class should only be used by code in the data-impl module.
     */
    struct ReadGroup
    {
        /**
         * Per-server for a newsgroup that's been read.
         *
         * This private class should only be used by code in the data-impl
         * module.
         */
        struct Server
        {
        };

        typedef Loki::AssocVector<Quark, Server> servers_t;
        servers_t _servers;

        Server &operator[](Quark const &s)
        {
          return _servers[s];
        }

        Server *find_server(Quark const &s)
        {
          servers_t::iterator it(_servers.find(s));
          return it == _servers.end() ? nullptr : &it->second;
        }

        Server const *find_server(Quark const &s) const
        {
          servers_t::const_iterator it(_servers.find(s));
          return it == _servers.end() ? nullptr : &it->second;
        }
    };

    void migrate_group_descriptions(DataIO const &);
    void migrate_group_xovers(DataIO const &);

    void migrate_group_permissions(DataIO const &);

    std::string get_newsrc_filename(Quark const &server) const;
    void migrate_newsrc(Quark const &server, LineReader *);
    void migrate_newsrc_files(DataIO const &);
    void load_groups_from_db();
    void save_new_groups_in_db(Quark const &server_pan_id,
                               NewGroup const *newgroups,
                               int count);
    void save_group_descriptions_in_db(NewGroup const *new_groups, int count);

  public: // mutators
    void add_groups(Quark const &server,
                    NewGroup const *new_groups,
                    size_t group_count) override;

    void add_group_in_db(StringView const &server_pan_id, StringView const &group, bool pseudo = false);
    void add_group_in_db(Quark const &server_pan_id, Quark const &group, bool pseudo = false);

    void mark_group_read(Quark const &group) override;

    void set_group_subscribed(Quark const &group, bool sub) override;

  public: // accessors
    const std::string get_group_description(Quark const &group) const override;
    void get_subscribed_groups(std::vector<Quark> &) const override;
    void get_other_groups(std::vector<Quark> &) const override;
    virtual void get_group_counts(Quark const &group,
                                  Article_Count &setme_unread,
                                  Article_Count &setme_total) const override;
    char get_group_permission(Quark const &group) const override;
    void group_get_servers(Quark const &group, quarks_t &) const override;
    void server_get_groups(Quark const &server, quarks_t &) const override;

    void process_references(Quark message_id, std::string references);

    /**
    ***  HEADERS
    **/
  public:
    void delete_articles(std::vector<Quark> const &goners);
    void delete_one_article(Quark const goner);

  private: // implementation
    void store_parent_articles(Quark &message_id, std::string &references);

    void insert_part_in_db(Quark const &g,
                           Quark const &mid,
                           int number,
                           StringView const &part_mid,
                           unsigned long lines,
                           unsigned long bytes);

    void migrate_headers(DataIO const &, Quark const &group);
    void update_part_states(Quark const &group) override;
    void migrate_read_ranges(Quark const &group);
    void load_headers_from_db(Quark const &group);

    void create_ghost_article(const Quark &old_msg_id, const int64_t old_id);
    void delete_ghost_articles(const Quark &old_msg_id,
                               const std::vector<std::string> &ghost_ids);
    void get_ghost_articles_to_delete(const Quark &old_msg_id,
                                      const int64_t old_id,
                                      std::vector<std::string> &setme);

    void fire_article_flag_changed(articles_t &a, Quark const &group) override;

    struct GroupHeaders
    {
        int _ref;
        bool _dirty;
        MemChunk<Article> _art_chunk;

        GroupHeaders();
        ~GroupHeaders();

        Article &alloc_new_article()
        {
          static Article const blank_article;
          _art_chunk.push_back(blank_article);
          return _art_chunk.back();
        }

        void reserve(Article_Count);
    };

    void get_article_references(Article const *,
                                std::string &setme) const override;

    typedef Loki::AssocVector<Quark, GroupHeaders *> group_to_headers_t;
    group_to_headers_t _group_to_headers;

    GroupHeaders *get_group_headers(Quark const &group);
    GroupHeaders const *get_group_headers(Quark const &group) const;
    void free_group_headers_memory(Quark const &group);
    bool is_read(Xref const &) const;

    void ref_group(Quark const &group);
    void unref_group(Quark const &group);

  private:
    class MyTree final : public Data::ArticleTree
    {
        friend class DataImpl;

      public: // life cycle
        MyTree(DataImpl &data_impl,
               Quark const &group,
               Quark const &save_path, // for auto-download
               Data::ShowType const show_type,
               FilterInfo const *filter_info = nullptr,
               RulesInfo const *rules = nullptr);
        virtual ~MyTree();

      protected:
        void fire_updates() const ;

      public: // from ArticleTree
        void reset_article_view() const;
        void initialize_article_view_table() const override;
        void update_article_view_table() const override;
        void mark_as_pending_deletion(const std::set<const Article*>) const override;
        Article get_parent(Quark const &mid) const override;
        bool has_article() const override;
        void update_article_tables_after_gui_update() const override;
        void set_article_hidden_status(quarks_t &mids) const override;
        void set_filter(const ShowType show_type = SHOW_ARTICLES,
                        FilterInfo const *criteria = nullptr) final override;
        void set_rules(RulesInfo const *rules = nullptr) final override;
        int
        get_threads(std::vector<Data::ArticleTree::Thread> &threads,
                    header_column_enum sort_column, bool sort_ascending,
                    std::string status_cond = "") const override;
        int get_shown_threads(
          std::vector<Data::ArticleTree::Thread> &threads,
          header_column_enum sort_column, bool sort_ascending) const override;
        int get_sorted_shown_threads(
            std::vector<Data::ArticleTree::Thread> &threads,
            header_column_enum header_column_id = COL_DATE,
            bool ascending = false) const override;
        int get_exposed_articles(
          std::vector<Data::ArticleTree::Thread> &threads,
          header_column_enum header_column_id = COL_DATE,
          bool ascending = false) const override;
        int call_on_reparented_articles(
          std::function<void(Quark msg_id, Quark new_parent_id)> cb) const override;
        int get_hidden_articles(quarks_t &fillme) const override;
        void get_shown_parent_ids(std::vector<Quark> &shown_parents_ids) const override;

      private:
        void set_parent_in_article_view() const;
        int fill_article_view_from_article() const;
        void set_join_and_column(header_column_enum &header_column_id,
                                 std::string &db_column,
                                 std::string &db_join) const;

      public:
        void articles_changed(bool do_refilter);
        void add_articles(quarks_t const &mids);

      private: // implementation fields
        const Quark _group;
        DataImpl &_data;
        const Quark _save_path; // for auto-download
        FilterInfo _filter;
        RulesInfo _rules;
        Data::ShowType _show_type;
        HeaderFilter _header_filter;
        HeaderRules _header_rules;

      private:
        void cache_articles(std::set<Article const *> s);
        void download_articles(std::set<Article const *> s);
    };

    MyTree *_tree;
    void on_articles_added(Quark const &group, quarks_t const &mids);
    void on_articles_changed(Quark const &group,
                             quarks_t const &mids,
                             bool do_refilter);
    void remove_articles_from_tree(MyTree *, quarks_t const &mids) const;
    void add_articles_to_tree(MyTree *, quarks_t const &mids);

  public: // Data interface
    void delete_articles(unique_articles_t const &) override;
    void delete_articles(std::vector<Article> const &) override;
    void delete_orphan_author() override;

    ArticleTree *group_get_articles(
      Quark const &group,
      Quark const &save_path,
      const ShowType show_type = SHOW_ARTICLES,
      FilterInfo const *criteria = nullptr,
      RulesInfo const *rules = nullptr) const override;

    void group_clear_articles(Quark const &group) override;

    bool is_read(Article const *) const override;

    void mark_read(Article const &article, bool mark_read) override;

    void mark_read(Article const **articles,
                   unsigned long article_count,
                   bool mark_read = true) override;

    void get_article_scores(Quark const &newsgroup,
                            Article const &article,
                            Scorefile::items_t &setme) const override;

    void add_score(StringView const &section_wildmat,
                   int score_value,
                   bool score_assign_flag,
                   int lifespan_days,
                   bool all_items_must_be_true,
                   Scorefile::AddItem const *items,
                   size_t item_count,
                   bool do_rescore) override;

    void comment_out_scorefile_line(StringView const &filename,
                                    size_t begin_line,
                                    size_t end_line,
                                    bool do_rescore) override;

    void rescore_articles(Quark const &group, const quarks_t mids) override;

    void rescore_group_articles(Quark const &group) override;

    void rescore() override;

    std::string get_scorefile_name() const override;

  private:
    Scorefile _scorefile;

    /**
    ***  XOVER
    **/

  private: // implementation
    /**
     * This is a workarea used when we processing an XOVER command.
     */
    struct XOverEntry
    {
        time_t _last_flush_time;

        /** These are the articles which have been recently added.
            The patch is periodically flushed to on_articles_added()
            from xover_line() and xref_unref(). */
        quarks_t _added_batch;

        /** Same as _added_batch, but for changed articles. */
        quarks_t _changed_batch;

        /** We must refcount because multiple server connections can
            be assigned to the same XOVER task. */
        int refcount;

        XOverEntry() :
          _last_flush_time(0),
          refcount(0)
        {
        }
    };

    typedef Loki::AssocVector<Quark, XOverEntry> xovers_t;
    xovers_t _xovers;

    XOverEntry *_cached_xover_entry;
    Quark _cached_xover_group;

    // used to cache subject to article mid
    std::unordered_map<std::string, Quark> _mid_cache;

    /**
     * Destroy the workarea.
     * Don't call this directly -- xover_unref() do its job.
     */
    void xover_clear_workarea(Quark const &group);

    /**
     * Finds the XOverEntry workarea for the specified group.
     * This must be called inside an xover_ref() / xover_unref() block,
     * as the workarea is instantiated when the group's xover refcount
     * increases to one and destroyed when it goes down to zero.
     */
    XOverEntry &xover_get_workarea(Quark const &group);

  public: // Data interface
    void xover_ref(Quark const &group) override;

    Article const *xover_add(Quark const &server,
                             Quark const &group,
                             StringView const &subject,
                             StringView const &author,
                             const time_t date,
                             StringView const &message_id,
                             StringView const &references,
                             unsigned long const byte_count,
                             unsigned long const line_count,
                             StringView const &xref,
                             bool const is_virtual = false) override;
    void insert_xref_in_db(Quark const &server,
                           Quark const msg_id,
                           StringView const &line);


    /** useful for xover unit testing */
    virtual void xover_flush(Quark const &group);

    virtual void xover_unref(Quark const &group) override;

    virtual Article_Number get_xover_high(Quark const &group,
                                          Quark const &server) const override;

    virtual void set_xover_high(Quark const &group,
                                Quark const &server,
                                const Article_Number high) override;

    /**
    *** TaskArchive
    **/

  public:
    void save_tasks(std::vector<Task *> const &saveme) override;

    void load_tasks(std::vector<Task *> &setme) override;

  public:
    const ArticleFilter _article_filter;
    const HeaderFilter _header_filter;
};
} // namespace pan

#endif
