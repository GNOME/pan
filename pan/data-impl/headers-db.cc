/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * SQLite backend for article headers.
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "headers-db.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace pan {

/* ======================================================================= */
/*  Schema                                                                   */
/* ======================================================================= */

static const char SCHEMA_SQL[] = R"sql(
PRAGMA journal_mode = WAL;
PRAGMA synchronous  = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA temp_store   = MEMORY;
PRAGMA cache_size   = -32768;   /* 32 MB page cache */

CREATE TABLE IF NOT EXISTS articles (
    id          INTEGER PRIMARY KEY,
    group_name  TEXT    NOT NULL,
    message_id  TEXT    NOT NULL,
    author      TEXT    NOT NULL DEFAULT '',
    subject     TEXT    NOT NULL DEFAULT '',
    time_posted INTEGER NOT NULL DEFAULT 0,
    lines       INTEGER NOT NULL DEFAULT 0,
    bytes       INTEGER NOT NULL DEFAULT 0,
    score       INTEGER NOT NULL DEFAULT 0,
    is_binary   INTEGER NOT NULL DEFAULT 0,
    flag        INTEGER NOT NULL DEFAULT 0,
    parent_mid  TEXT    NOT NULL DEFAULT '',
    total_parts INTEGER NOT NULL DEFAULT 1,
    UNIQUE (group_name, message_id)
);

/* Most common query: list a group sorted by time, paged. */
CREATE INDEX IF NOT EXISTS idx_art_group_time
    ON articles (group_name, time_posted DESC);

/* Threading: walk children of a parent. */
CREATE INDEX IF NOT EXISTS idx_art_parent
    ON articles (group_name, parent_mid)
    WHERE parent_mid != '';

/* Subject sort for grouped display. */
CREATE INDEX IF NOT EXISTS idx_art_group_subject
    ON articles (group_name, subject);

CREATE TABLE IF NOT EXISTS parts (
    article_id  INTEGER NOT NULL REFERENCES articles(id) ON DELETE CASCADE,
    part_number INTEGER NOT NULL,
    part_mid    TEXT    NOT NULL DEFAULT '',
    bytes       INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (article_id, part_number)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS xrefs (
    article_id     INTEGER NOT NULL REFERENCES articles(id) ON DELETE CASCADE,
    server         TEXT    NOT NULL,
    xref_group     TEXT    NOT NULL,
    article_number INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (article_id, server, xref_group)
) WITHOUT ROWID;

/* Per-group-per-server XOVER high-water mark. */
CREATE TABLE IF NOT EXISTS group_server_state (
    group_name TEXT NOT NULL,
    server     TEXT NOT NULL,
    xover_high INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (group_name, server)
) WITHOUT ROWID;

/* FTS5 index: subject + author, backed by the articles table. */
CREATE VIRTUAL TABLE IF NOT EXISTS articles_fts USING fts5(
    subject,
    author,
    content  = 'articles',
    content_rowid = 'id'
);

/* Keep FTS in sync with the articles table. */
CREATE TRIGGER IF NOT EXISTS articles_ai
AFTER INSERT ON articles BEGIN
    INSERT INTO articles_fts (rowid, subject, author)
    VALUES (new.id, new.subject, new.author);
END;

CREATE TRIGGER IF NOT EXISTS articles_ad
AFTER DELETE ON articles BEGIN
    INSERT INTO articles_fts (articles_fts, rowid, subject, author)
    VALUES ('delete', old.id, old.subject, old.author);
END;

CREATE TRIGGER IF NOT EXISTS articles_au
AFTER UPDATE OF subject, author ON articles BEGIN
    INSERT INTO articles_fts (articles_fts, rowid, subject, author)
    VALUES ('delete', old.id, old.subject, old.author);
    INSERT INTO articles_fts (rowid, subject, author)
    VALUES (new.id, new.subject, new.author);
END;
)sql";

/* ======================================================================= */
/*  Prepared statement SQL                                                   */
/* ======================================================================= */

static const char SQL_UPSERT_ARTICLE[] =
    "INSERT INTO articles"
    "  (group_name, message_id, author, subject, time_posted,"
    "   lines, bytes, score, is_binary, flag, parent_mid, total_parts)"
    " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)"
    " ON CONFLICT(group_name, message_id) DO UPDATE SET"
    "  author      = excluded.author,"
    "  subject     = excluded.subject,"
    "  time_posted = excluded.time_posted,"
    "  lines       = excluded.lines,"
    "  bytes       = excluded.bytes,"
    "  score       = excluded.score,"
    "  is_binary   = excluded.is_binary,"
    "  parent_mid  = excluded.parent_mid,"
    "  total_parts = excluded.total_parts"
    " RETURNING id";

static const char SQL_INSERT_PART[] =
    "INSERT OR IGNORE INTO parts (article_id, part_number, part_mid, bytes)"
    " VALUES (?,?,?,?)";

static const char SQL_INSERT_XREF[] =
    "INSERT OR IGNORE INTO xrefs (article_id, server, xref_group, article_number)"
    " VALUES (?,?,?,?)";

static const char SQL_GET_ARTICLE[] =
    "SELECT id, message_id, author, subject, time_posted,"
    "       lines, bytes, score, is_binary, flag, parent_mid, total_parts"
    " FROM articles"
    " WHERE group_name=? AND message_id=?"
    " LIMIT 1";

static const char SQL_COUNT[] =
    "SELECT COUNT(*) FROM articles WHERE group_name=?";

static const char SQL_XOVER_GET[] =
    "SELECT xover_high FROM group_server_state"
    " WHERE group_name=? AND server=?";

static const char SQL_XOVER_SET[] =
    "INSERT INTO group_server_state (group_name, server, xover_high)"
    " VALUES (?,?,?)"
    " ON CONFLICT(group_name, server) DO UPDATE SET xover_high=excluded.xover_high";

static const char SQL_SET_SCORE[] =
    "UPDATE articles SET score=? WHERE group_name=? AND message_id=?";

static const char SQL_SET_FLAG[] =
    "UPDATE articles SET flag=? WHERE group_name=? AND message_id=?";

static const char SQL_GET_PARTS[] =
    "SELECT part_number, part_mid, bytes FROM parts"
    " WHERE article_id=? ORDER BY part_number";

static const char SQL_GET_XREFS[] =
    "SELECT server, xref_group, article_number FROM xrefs"
    " WHERE article_id=?";

/* ======================================================================= */
/*  Helpers                                                                  */
/* ======================================================================= */

static inline const char *col_text(sqlite3_stmt *st, int col)
{
    const char *t = reinterpret_cast<const char *>(sqlite3_column_text(st, col));
    return t ? t : "";
}

HeadersDB::Row HeadersDB::row_from_stmt(sqlite3_stmt *st)
{
    Row r;
    r.id          = sqlite3_column_int64(st, 0);
    r.message_id  = col_text(st, 1);
    r.author      = col_text(st, 2);
    r.subject     = col_text(st, 3);
    r.time_posted = sqlite3_column_int64(st, 4);
    r.lines       = sqlite3_column_int(st, 5);
    r.bytes       = sqlite3_column_int64(st, 6);
    r.score       = sqlite3_column_int(st, 7);
    r.is_binary   = sqlite3_column_int(st, 8) != 0;
    r.flag        = sqlite3_column_int(st, 9) != 0;
    r.parent_mid  = col_text(st, 10);
    r.total_parts = sqlite3_column_int(st, 11);
    return r;
}

bool HeadersDB::exec(const char *sql)
{
    char *errmsg = nullptr;
    int rc = sqlite3_exec(_db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        _last_error = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

void HeadersDB::set_error(const char *context)
{
    _last_error = context;
    _last_error += ": ";
    _last_error += sqlite3_errmsg(_db);
}

/* ======================================================================= */
/*  Construction / destruction                                               */
/* ======================================================================= */

HeadersDB::HeadersDB(const std::string &db_path)
{
    int rc = sqlite3_open(db_path.c_str(), &_db);
    if (rc != SQLITE_OK)
    {
        _last_error = sqlite3_errmsg(_db);
        sqlite3_close(_db);
        _db = nullptr;
        return;
    }

    /* 5-second busy timeout so concurrent readers don't immediately fail. */
    sqlite3_busy_timeout(_db, 5000);

    create_schema();
    if (_db)
        prepare_statements();
}

HeadersDB::~HeadersDB()
{
    finalize_statements();
    if (_db)
    {
        sqlite3_close(_db);
        _db = nullptr;
    }
}

void HeadersDB::create_schema()
{
    /* Execute the schema SQL in one shot. sqlite3_exec handles multiple
       statements separated by semicolons. */
    char *errmsg = nullptr;
    int rc = sqlite3_exec(_db, SCHEMA_SQL, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        _last_error = errmsg ? errmsg : "schema creation failed";
        sqlite3_free(errmsg);
        sqlite3_close(_db);
        _db = nullptr;
    }
}

static void prepare_one(sqlite3 *db, const char *sql, sqlite3_stmt **out)
{
    int rc = sqlite3_prepare_v2(db, sql, -1, out, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("sqlite3_prepare_v2: ") + sqlite3_errmsg(db));
}

void HeadersDB::prepare_statements()
{
    try
    {
        prepare_one(_db, SQL_UPSERT_ARTICLE, &_st_upsert_article);
        prepare_one(_db, SQL_INSERT_PART,    &_st_insert_part);
        prepare_one(_db, SQL_INSERT_XREF,    &_st_insert_xref);
        prepare_one(_db, SQL_GET_ARTICLE,    &_st_get_article);
        prepare_one(_db, SQL_COUNT,          &_st_count);
        prepare_one(_db, SQL_XOVER_GET,      &_st_xover_get);
        prepare_one(_db, SQL_XOVER_SET,      &_st_xover_set);
        prepare_one(_db, SQL_SET_SCORE,      &_st_set_score);
        prepare_one(_db, SQL_SET_FLAG,       &_st_set_flag);
        prepare_one(_db, SQL_GET_PARTS,      &_st_get_parts);
        prepare_one(_db, SQL_GET_XREFS,      &_st_get_xrefs);
    }
    catch (std::exception const &e)
    {
        _last_error = e.what();
        sqlite3_close(_db);
        _db = nullptr;
    }
}

void HeadersDB::finalize_statements()
{
    sqlite3_finalize(_st_upsert_article); _st_upsert_article = nullptr;
    sqlite3_finalize(_st_insert_part);    _st_insert_part    = nullptr;
    sqlite3_finalize(_st_insert_xref);    _st_insert_xref    = nullptr;
    sqlite3_finalize(_st_get_article);    _st_get_article    = nullptr;
    sqlite3_finalize(_st_count);          _st_count          = nullptr;
    sqlite3_finalize(_st_xover_get);      _st_xover_get      = nullptr;
    sqlite3_finalize(_st_xover_set);      _st_xover_set      = nullptr;
    sqlite3_finalize(_st_set_score);      _st_set_score      = nullptr;
    sqlite3_finalize(_st_set_flag);       _st_set_flag       = nullptr;
    sqlite3_finalize(_st_get_parts);      _st_get_parts      = nullptr;
    sqlite3_finalize(_st_get_xrefs);      _st_get_xrefs      = nullptr;
}

/* ======================================================================= */
/*  Batch insert                                                             */
/* ======================================================================= */

void HeadersDB::begin_batch()
{
    if (!_db) return;
    _in_batch = true;
    exec("BEGIN");
}

void HeadersDB::commit_batch()
{
    if (!_db) return;
    exec("COMMIT");
    _in_batch = false;
}

void HeadersDB::rollback_batch()
{
    if (!_db) return;
    exec("ROLLBACK");
    _in_batch = false;
}

int64_t HeadersDB::upsert_article(const std::string &group, const Row &row)
{
    if (!_db) return -1;

    sqlite3_stmt *st = _st_upsert_article;
    sqlite3_reset(st);

    sqlite3_bind_text(st,  1, group.c_str(),          -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  2, row.message_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  3, row.author.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  4, row.subject.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, row.time_posted);
    sqlite3_bind_int(st,   6, row.lines);
    sqlite3_bind_int64(st, 7, row.bytes);
    sqlite3_bind_int(st,   8, row.score);
    sqlite3_bind_int(st,   9, row.is_binary ? 1 : 0);
    sqlite3_bind_int(st,  10, row.flag      ? 1 : 0);
    sqlite3_bind_text(st, 11, row.parent_mid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st,  12, row.total_parts);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(st, 0);

    set_error("upsert_article");
    return -1;
}

void HeadersDB::insert_part(int64_t article_id,
                             int32_t part_number,
                             const std::string &part_mid,
                             int64_t bytes)
{
    if (!_db) return;

    sqlite3_stmt *st = _st_insert_part;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, article_id);
    sqlite3_bind_int(st,   2, part_number);
    sqlite3_bind_text(st,  3, part_mid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, bytes);

    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE)
        set_error("insert_part");
}

void HeadersDB::insert_xref(int64_t article_id,
                              const std::string &server,
                              const std::string &xref_group,
                              int64_t article_number)
{
    if (!_db) return;

    sqlite3_stmt *st = _st_insert_xref;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, article_id);
    sqlite3_bind_text(st,  2, server.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  3, xref_group.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, article_number);

    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE)
        set_error("insert_xref");
}

/* ======================================================================= */
/*  Queries                                                                  */
/* ======================================================================= */

std::vector<HeadersDB::Row>
HeadersDB::load_window(const std::string &group,
                       int64_t offset,
                       int limit,
                       const std::string &sort_col,
                       bool descending) const
{
    std::vector<Row> out;
    if (!_db) return out;

    /* Whitelist sort columns to prevent SQL injection. */
    const char *col = "time_posted";
    if (sort_col == "subject")     col = "subject";
    else if (sort_col == "author") col = "author";
    else if (sort_col == "lines")  col = "lines";
    else if (sort_col == "score")  col = "score";

    std::string sql =
        "SELECT id, message_id, author, subject, time_posted,"
        "       lines, bytes, score, is_binary, flag, parent_mid, total_parts"
        " FROM articles WHERE group_name=?";
    sql += " ORDER BY ";
    sql += col;
    sql += descending ? " DESC" : " ASC";
    sql += " LIMIT ? OFFSET ?";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
        return out;

    sqlite3_bind_text(st, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st,  2, limit);
    sqlite3_bind_int64(st, 3, offset);

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(row_from_stmt(st));

    sqlite3_finalize(st);
    return out;
}

bool HeadersDB::get_article(const std::string &group,
                             const std::string &mid,
                             Row &out) const
{
    if (!_db) return false;

    sqlite3_stmt *st = _st_get_article;
    sqlite3_reset(st);
    sqlite3_bind_text(st, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, mid.c_str(),   -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) == SQLITE_ROW)
    {
        out = row_from_stmt(st);
        return true;
    }
    return false;
}

std::vector<HeadersDB::Row>
HeadersDB::load_thread(const std::string &group,
                       const std::string &root_mid) const
{
    std::vector<Row> out;
    if (!_db) return out;

    /* Recursive CTE: walk down from root_mid via parent_mid links. */
    static const char SQL[] =
        "WITH RECURSIVE thread(id) AS ("
        "  SELECT id FROM articles WHERE group_name=? AND message_id=?"
        "  UNION ALL"
        "  SELECT a.id FROM articles a"
        "  JOIN thread t ON a.parent_mid = (SELECT message_id FROM articles WHERE id=t.id)"
        "               AND a.group_name = ?"
        ")"
        "SELECT a.id, a.message_id, a.author, a.subject, a.time_posted,"
        "       a.lines, a.bytes, a.score, a.is_binary, a.flag,"
        "       a.parent_mid, a.total_parts"
        " FROM articles a"
        " JOIN thread t ON a.id = t.id"
        " ORDER BY a.time_posted ASC";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, SQL, -1, &st, nullptr) != SQLITE_OK)
        return out;

    sqlite3_bind_text(st, 1, group.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, root_mid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, group.c_str(),    -1, SQLITE_TRANSIENT);

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(row_from_stmt(st));

    sqlite3_finalize(st);
    return out;
}

std::vector<HeadersDB::Row>
HeadersDB::search_fts(const std::string &group,
                      const std::string &query,
                      int limit) const
{
    std::vector<Row> out;
    if (!_db) return out;

    static const char SQL[] =
        "SELECT a.id, a.message_id, a.author, a.subject, a.time_posted,"
        "       a.lines, a.bytes, a.score, a.is_binary, a.flag,"
        "       a.parent_mid, a.total_parts"
        " FROM articles_fts f"
        " JOIN articles a ON a.id = f.rowid"
        " WHERE articles_fts MATCH ? AND a.group_name = ?"
        " ORDER BY rank"
        " LIMIT ?";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, SQL, -1, &st, nullptr) != SQLITE_OK)
        return out;

    sqlite3_bind_text(st, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st,  3, limit);

    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(row_from_stmt(st));

    sqlite3_finalize(st);
    return out;
}

int64_t HeadersDB::count(const std::string &group) const
{
    if (!_db) return 0;

    sqlite3_stmt *st = _st_count;
    sqlite3_reset(st);
    sqlite3_bind_text(st, 1, group.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) == SQLITE_ROW)
        return sqlite3_column_int64(st, 0);
    return 0;
}

std::vector<HeadersDB::PartRow> HeadersDB::get_parts(int64_t article_id) const
{
    std::vector<PartRow> out;
    if (!_db) return out;

    sqlite3_stmt *st = _st_get_parts;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, article_id);

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        PartRow p;
        p.part_number = sqlite3_column_int(st, 0);
        p.part_mid    = col_text(st, 1);
        p.bytes       = sqlite3_column_int64(st, 2);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<HeadersDB::XrefRow> HeadersDB::get_xrefs(int64_t article_id) const
{
    std::vector<XrefRow> out;
    if (!_db) return out;

    sqlite3_stmt *st = _st_get_xrefs;
    sqlite3_reset(st);
    sqlite3_bind_int64(st, 1, article_id);

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        XrefRow x;
        x.server         = col_text(st, 0);
        x.xref_group     = col_text(st, 1);
        x.article_number = sqlite3_column_int64(st, 2);
        out.push_back(std::move(x));
    }
    return out;
}

/* ======================================================================= */
/*  XOVER high-water mark                                                    */
/* ======================================================================= */

int64_t HeadersDB::get_xover_high(const std::string &group,
                                   const std::string &server) const
{
    if (!_db) return 0;

    sqlite3_stmt *st = _st_xover_get;
    sqlite3_reset(st);
    sqlite3_bind_text(st, 1, group.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, server.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) == SQLITE_ROW)
        return sqlite3_column_int64(st, 0);
    return 0;
}

void HeadersDB::set_xover_high(const std::string &group,
                                const std::string &server,
                                int64_t high)
{
    if (!_db) return;

    sqlite3_stmt *st = _st_xover_set;
    sqlite3_reset(st);
    sqlite3_bind_text(st,  1, group.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  2, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, high);

    if (sqlite3_step(st) != SQLITE_DONE)
        set_error("set_xover_high");
}

/* ======================================================================= */
/*  Mutations                                                                */
/* ======================================================================= */

void HeadersDB::set_score(const std::string &group,
                           const std::string &mid,
                           int32_t score)
{
    if (!_db) return;

    sqlite3_stmt *st = _st_set_score;
    sqlite3_reset(st);
    sqlite3_bind_int(st,   1, score);
    sqlite3_bind_text(st,  2, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  3, mid.c_str(),   -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE)
        set_error("set_score");
}

void HeadersDB::set_flag(const std::string &group,
                          const std::string &mid,
                          bool flag)
{
    if (!_db) return;

    sqlite3_stmt *st = _st_set_flag;
    sqlite3_reset(st);
    sqlite3_bind_int(st,   1, flag ? 1 : 0);
    sqlite3_bind_text(st,  2, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  3, mid.c_str(),   -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE)
        set_error("set_flag");
}

void HeadersDB::expire_articles(const std::string &group,
                                 const std::string &server,
                                 int64_t below_number)
{
    if (!_db) return;

    static const char SQL[] =
        "DELETE FROM articles WHERE id IN ("
        "  SELECT a.id FROM articles a"
        "  JOIN xrefs x ON x.article_id = a.id"
        "  WHERE a.group_name=? AND x.server=? AND x.article_number < ?"
        ")";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, SQL, -1, &st, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(st,  1, group.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  2, server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, below_number);

    sqlite3_step(st);
    sqlite3_finalize(st);
}

int64_t HeadersDB::expire_articles_before_time(const std::string &group,
                                                int64_t cutoff_time)
{
    if (!_db) return 0;

    static const char SQL[] =
        "DELETE FROM articles WHERE group_name=? AND time_posted<?";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, SQL, -1, &st, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_text (st, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, cutoff_time);

    sqlite3_step(st);
    const int64_t deleted = sqlite3_changes64(_db);
    sqlite3_finalize(st);
    return deleted;
}

void HeadersDB::delete_group(const std::string &group)
{
    if (!_db) return;

    static const char SQL[] = "DELETE FROM articles WHERE group_name=?";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(_db, SQL, -1, &st, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(st, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);

    static const char SQL2[] =
        "DELETE FROM group_server_state WHERE group_name=?";
    if (sqlite3_prepare_v2(_db, SQL2, -1, &st, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(st, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* ======================================================================= */
/*  Maintenance                                                              */
/* ======================================================================= */

void HeadersDB::rebuild_fts()
{
    if (!_db) return;
    exec("INSERT INTO articles_fts(articles_fts) VALUES('rebuild')");
}

void HeadersDB::vacuum()
{
    if (!_db) return;
    exec("VACUUM");
}

} // namespace pan
