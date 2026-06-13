/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 * SQLite backend extension (C) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef __HeadersDB_h__
#define __HeadersDB_h__

#include <cstdint>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace pan {

/**
 * SQLite-backed store for article headers.
 *
 * Replaces the in-memory MemChunk + std::map approach in GroupHeaders,
 * enabling newsgroups with 30M+ articles without blowing the heap.
 *
 * Key design points:
 * - One SQLite file for all groups (~/.pan2/articles.db)
 * - FTS5 index on subject + author for fast search/filter
 * - Windowed queries instead of loading the entire group at once
 * - Bulk-insert via BEGIN/COMMIT for XOVER fetch performance
 */
class HeadersDB
{
  public:
    explicit HeadersDB(const std::string &db_path);
    ~HeadersDB();

    HeadersDB(const HeadersDB &) = delete;
    HeadersDB &operator=(const HeadersDB &) = delete;

    bool ok() const { return _db != nullptr; }
    std::string last_error() const { return _last_error; }

    /**
     * A single article row as returned by query methods.
     */
    struct Row
    {
        int64_t     id          = 0;
        std::string message_id;
        std::string author;
        std::string subject;
        int64_t     time_posted = 0;
        int32_t     lines       = 0;
        int64_t     bytes       = 0;
        int32_t     score       = 0;
        bool        is_binary   = false;
        bool        flag        = false;
        std::string parent_mid;
        int32_t     total_parts = 1;
    };

    /**
     * A single part entry (for multipart articles).
     */
    struct PartRow
    {
        int32_t     part_number = 1;
        std::string part_mid;
        int64_t     bytes       = 0;
    };

    /**
     * A cross-reference entry.
     */
    struct XrefRow
    {
        std::string server;
        std::string xref_group;
        int64_t     article_number = 0;
    };

    /* ------------------------------------------------------------------ */
    /*  Batch insert (use during XOVER fetch for maximum throughput)       */
    /* ------------------------------------------------------------------ */

    void begin_batch();
    void commit_batch();
    void rollback_batch();

    /**
     * Insert or update an article.
     * Returns the rowid of the article (new or existing).
     * Call within begin_batch() / commit_batch() for speed.
     */
    int64_t upsert_article(const std::string &group, const Row &row);

    void insert_part(int64_t article_id,
                     int32_t part_number,
                     const std::string &part_mid,
                     int64_t bytes);

    void insert_xref(int64_t article_id,
                     const std::string &server,
                     const std::string &xref_group,
                     int64_t article_number);

    /* ------------------------------------------------------------------ */
    /*  Queries                                                             */
    /* ------------------------------------------------------------------ */

    /**
     * Load a page of articles sorted by time_posted (newest first by default).
     * Used for the main article-list view with virtual/lazy scrolling.
     */
    std::vector<Row> load_window(const std::string &group,
                                 int64_t offset,
                                 int limit,
                                 const std::string &sort_col = "time_posted",
                                 bool descending = true) const;

    /** Lookup a single article by message-id. */
    bool get_article(const std::string &group,
                     const std::string &mid,
                     Row &out) const;

    /**
     * Return all articles that form the thread rooted at root_mid.
     * Uses a recursive CTE to walk parent_mid links.
     */
    std::vector<Row> load_thread(const std::string &group,
                                 const std::string &root_mid) const;

    /**
     * FTS5 full-text search on subject and author.
     * query follows FTS5 match syntax (e.g. "nzb OR yenc", "subject:foo").
     */
    std::vector<Row> search_fts(const std::string &group,
                                const std::string &query,
                                int limit = 5000) const;

    /** Number of articles stored for a group. */
    int64_t count(const std::string &group) const;

    /** Parts for a given article rowid. */
    std::vector<PartRow> get_parts(int64_t article_id) const;

    /** Xrefs for a given article rowid. */
    std::vector<XrefRow> get_xrefs(int64_t article_id) const;

    /* ------------------------------------------------------------------ */
    /*  XOVER high-water mark (per group + server)                         */
    /* ------------------------------------------------------------------ */

    int64_t get_xover_high(const std::string &group,
                           const std::string &server) const;

    void set_xover_high(const std::string &group,
                        const std::string &server,
                        int64_t high);

    /* ------------------------------------------------------------------ */
    /*  Mutations                                                           */
    /* ------------------------------------------------------------------ */

    void set_score(const std::string &group,
                   const std::string &mid,
                   int32_t score);

    void set_flag(const std::string &group,
                  const std::string &mid,
                  bool flag);

    /** Remove articles with article_number < cutoff for the given server. */
    void expire_articles(const std::string &group,
                         const std::string &server,
                         int64_t below_number);

    /**
     * Remove articles in @p group posted before @p cutoff_time (unix epoch).
     * ON DELETE CASCADE removes matching parts and xrefs automatically.
     * Returns the number of articles deleted.
     */
    int64_t expire_articles_before_time(const std::string &group,
                                        int64_t cutoff_time);

    /** Remove all articles for a group (e.g. when unsubscribing). */
    void delete_group(const std::string &group);

    /* ------------------------------------------------------------------ */
    /*  Maintenance                                                         */
    /* ------------------------------------------------------------------ */

    /** Rebuild the FTS5 index (call after bulk import from old flat files). */
    void rebuild_fts();

    /** Run VACUUM to reclaim disk space after deletions. */
    void vacuum();

  private:
    sqlite3 *_db = nullptr;
    std::string _last_error;
    bool _in_batch = false;

    /* prepared statements */
    sqlite3_stmt *_st_upsert_article  = nullptr;
    sqlite3_stmt *_st_insert_part     = nullptr;
    sqlite3_stmt *_st_insert_xref     = nullptr;
    sqlite3_stmt *_st_get_article     = nullptr;
    sqlite3_stmt *_st_count           = nullptr;
    sqlite3_stmt *_st_xover_get       = nullptr;
    sqlite3_stmt *_st_xover_set       = nullptr;
    sqlite3_stmt *_st_set_score       = nullptr;
    sqlite3_stmt *_st_set_flag        = nullptr;
    sqlite3_stmt *_st_get_parts       = nullptr;
    sqlite3_stmt *_st_get_xrefs       = nullptr;

    void create_schema();
    void prepare_statements();
    void finalize_statements();

    bool exec(const char *sql);
    void set_error(const char *context);

    static Row row_from_stmt(sqlite3_stmt *stmt);
};

} // namespace pan
#endif
