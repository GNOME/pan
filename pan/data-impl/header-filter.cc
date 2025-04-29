#include "header-filter.h"
#include "pan/data/data.h"
#include "pan/general/log4cxx.h"
#include "pan/usenet-utils/filter-info.h"
#include "pan/usenet-utils/scorefile.h"
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <log4cxx/logger.h>
#include <string>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("header-filter"));
}

bool HeaderFilter::score_article(
  Data const &data,
  std::deque<Scorefile::Section> const &sections,
  Article const &article) const
{
  std::vector<Scorefile::Section> v_sections;
  for (Scorefile::Section s : sections)
  {
    v_sections.push_back(s);
  }
  return score_article(data, v_sections, article);
}

bool HeaderFilter::score_article(
  Data const &data,
  std::vector<Scorefile::Section> const &sections,
  Article const &article) const
{
  int score(0);
  for(Scorefile::Section const sit : sections) {
    foreach_const (Scorefile::items_t, sit.items, it)
    {
      if (it->expired)
      {
        continue;
      }
      SqlCond precond("message_id = ? and", article.message_id);
      auto q = get_sql_query(data, "count()", precond, it->test);
      int count;
      while (q.executeStep())
      {
        count = q.getColumn(0);
      }
      if (count == 0)
      {
        continue;
      }
      if (it->value_assign_flag)
      {
        score = it->value;
        goto endloop;
      }
      score += it->value;
    }
  }

endloop:

  SQLite::Statement q(pan_db, R"SQL(
    update article_group set score = $score
    where group_id = (select id from `group` where name = $name)
      and article_id = (select id from article where message_id = $msg_id)
      and score != $score
  )SQL");
  q.bind(1, score);
  q.bind(2, article.group);
  q.bind(3, article.message_id);
  int n(q.exec());
  LOG4CXX_TRACE(logger,
                "Article " << article.message_id << " scored to " << score
                           << (n == 1 ? " (changed)" : " (unchanged)"));

  return (n == 1);
}

// returns a SQL statement like
// select message_id, passed from article join <..>
// where <group and criteria are matched>
SQLite::Statement HeaderFilter::get_sql_filter(Data const &data,
                                               Data::ShowType show_type,
                                               FilterInfo const &criteria) const
{
  std::function<std::string(std::string join, std::string where)> c;

  switch (show_type)
  {
    case pan::Data::SHOW_ARTICLES:
      c = [](std::string join, std::string where) -> std::string
      {
        std::string res("select message_id,");
        res += where.empty() ? "True" : "(" + where + ")";
        res += " from article\n";
        res += "join article_group as ag on ag.article_id = article.id\n";
        res += "join `group` as g on ag.group_id = g.id\n" + join;
        res += "where g.name == ? order by message_id\n";
        return res;
      };
      break;

    case pan::Data::SHOW_THREADS:
      c = [](std::string join, std::string where) -> std::string
      {
        std::string passed(
          "select article.id, article.parent_id, message_id,(" + where
          + ") as pass from article "
          + "join article_group as ag on ag.article_id = article.id "
          + "join `group` as g on ag.group_id = g.id " + join
          + "where g.name == ? ");

        std::string cte(R"SQL(
          with recursive
          -- list all articles with their test result (in pass)
          passed (id, parent_id, msg_id, pass) as (
        )SQL");

        cte += passed;
        cte += R"SQL(
          ),
          -- recursive part for parent
          parent (id, parent_id, msg_id,pass) as (
            -- list all articles with their state
            select id, parent_id, msg_id,pass from passed
            union ALL
            -- list all parents of passing articles. This parent were listed above, but now pass is True
            select article.id, article.parent_id, message_id,pass from parent
              join article on article.id == parent.parent_id
              where parent.pass == True
          ),
          -- recursive part for children
          child (id, parent_id, msg_id,pass) as (
            -- list all articles with their state
            select id, parent_id, msg_id,pass from passed
            union ALL
            -- list all children of passing articles. This parent were listed above, but now pass is True
            select article.id, article.parent_id, message_id,pass from child
              join article on article.parent_id == child.id
              where child.pass == True
          )
          -- aggregate the tests result, sum(pass) aggregate the tests result and the parent status
          select msg_id, sum(pass) > 0 as pass from (
            select msg_id, pass from parent
            union ALL
            select msg_id, pass from child
          ) group by msg_id
        )SQL";
        return cte;
      };
      break;

    case pan::Data::SHOW_SUBTHREADS:
      c = [](std::string join, std::string where) -> std::string
      {
        std::string passed(
          "select article.id, article.parent_id, message_id,(" + where
          + ") as pass from article "
          + "join article_group as ag on ag.article_id = article.id "
          + "join `group` as g on ag.group_id = g.id " + join
          + "where g.name == ? ");

        std::string cte(R"SQL(
          with recursive
          -- list all articles with their test result (in pass)
          passed (id, parent_id, msg_id, pass) as (
        )SQL");

        cte += passed;
        cte += R"SQL(
          ),
          -- recursive part for children
          child (id, parent_id, msg_id,pass) as (
            -- list all articles with their state
            select id, parent_id, msg_id,pass from passed
            union ALL
            -- list all children of passing articles. This parent were listed above, but now pass is True
            select article.id, article.parent_id, message_id,pass from child
              join article on article.parent_id == child.id
              where child.pass == True
          )
          -- aggregate the tests result, sum(pass) aggregate the tests result and the parent status
          select msg_id, sum(pass) > 0 as pass from child group by msg_id
        )SQL";
        return cte;
      };
      break;
  }
    return get_sql_query(data, c, criteria);
}

// returns a SQL statement like
// select message_id from article where <criteria is matched>
SQLite::Statement HeaderFilter::get_sql_query(Data const &data,
                                              FilterInfo const &criteria) const
{
    return get_sql_query(data, "message_id", SqlCond(), criteria);
}

// returns a SQL statement like
// select <select_str> from article where <criteria is matched>
SQLite::Statement HeaderFilter::get_sql_query(Data const &data,
                                              std::string const &select_str,
                                              FilterInfo const &criteria) const
{
  return get_sql_query(data, select_str, SqlCond(), criteria);
}

// returns a SQL statement like
// select <select_str> from article where <pre_cond> <criteria is matched>
SQLite::Statement HeaderFilter::get_sql_query(Data const &data,
                                              std::string const &select_str,
                                              SqlCond const &pre_cond,
                                              FilterInfo const &criteria) const
{
  auto c = [select_str](std::string join, std::string where) -> std::string
  {
    std::string q("select " + select_str + " from article");
    if (! join.empty())
    {
      q += " " + join;
    }
    if (! where.empty())
    {
      q += " where " + where;
    }
    return q;
  };

  return get_sql_query(data, c, pre_cond, criteria);
}

// returns a SQL statement like
// select <select_str> from article where <pre_cond> <criteria is matched>
SQLite::Statement HeaderFilter::get_sql_query(
  Data const &data,
  std::function<std::string(std::string join, std::string where)> compose,
  FilterInfo const &criteria) const
{
  return get_sql_query(data, compose, SqlCond(), criteria);
}

// returns a SQL statement like
// select <select_str> from article where <pre_cond> <criteria is matched>
// criteria parameters are already bound to returned query, so additional
// parameters must be bound with q.bind(q.getBindParameterCount(), thing)) AND
// additional parameters must be placed *after* "where" string in compose function
SQLite::Statement HeaderFilter::get_sql_query(
  Data const &data,
  std::function<std::string(std::string join, std::string where)> compose,
  SqlCond const &pre_cond,
  FilterInfo const &criteria) const
{
  auto sqlf = get_sql_filter(data, criteria);
  sqlf.insert(sqlf.begin(), pre_cond);

  std::string join, where;
  // add join clauses warning: there's a risk of adding identical join
  // clauses which may degrade perf or trigger errors
  std::for_each(sqlf.begin(),
                sqlf.end(),
                [&join](SqlCond const sc)
                {
                  if (! sc.join.empty())
                  {
                    join.append(sc.join + " ");
                  }
                });

  // add where clause to the SQL string
  std::for_each(sqlf.begin(),
                sqlf.end(),
                [&where](SqlCond const sc)
                {
                  if (! sc.where.empty())
                  {
                    where.append(sc.where + " ");
                  }
                });

  auto sql = compose(join, where);
  LOG4CXX_TRACE(logger, "SQL for header filter: «" << sql << "»");

  SQLite::Statement q(pan_db, sql);
  int i(1);
  // bind parameters to the query
  std::for_each(sqlf.begin(),
                sqlf.end(),
                [&q, &i](SqlCond const sc)
                {
                  switch (sc.type)
                  {
                    case pan::SqlCond::BOOL_PARAM:
                      LOG4CXX_TRACE(logger,
                                    "bool param " << i << " for header filter: "
                                                  << sc.bool_param);
                      q.bind(i++, sc.bool_param);
                      break;
                    case pan::SqlCond::INT_PARAM:
                      LOG4CXX_TRACE(logger,
                                    "int param " << i << " for header filter: "
                                                 << sc.int_param);
                      q.bind(i++, sc.int_param);
                      break;
                    case pan::SqlCond::STR_PARAM:
                      LOG4CXX_TRACE(logger,
                                    "str param " << i << " for header filter: "
                                                 << sc.str_param);
                      q.bind(i++, sc.str_param);
                      break;
                    default:
                      break;
                  }
                });
  return q;
};

SqlCond HeaderFilter::get_xref_sql_cond(Data const &data,
                                        FilterInfo const &criteria

) const
{
  // user is filtering by groupname?
  if (criteria._text._impl_type == TextMatch::CONTAINS)
  {
    std::string sql, param;
    if (criteria._text.create_sql_search("grp.name", sql, param))
    {
      SqlCond sc(sql, param);
      sc.join = "join `article_group` as ag on ag.article_id = article.id "
                "join `group` as grp on ag.group_id == grp.id";
      return sc;
    }
    return SqlCond();
  }
  // user is filtering by # of crossposts
  else if (criteria._text._impl_text.find("(.*:){") != std::string::npos)
  {
    // Scoring rule like: «Xref: (.*:){3}» or «~Xref: (.*:){11}» (at most)
    char const *search = "(.*:){"; //}
    std::string::size_type pos =
      criteria._text._impl_text.find(search) + strlen(search);
    int const ge = atoi(criteria._text._impl_text.c_str() + pos);
    FilterInfo tmp;
    tmp.set_type_crosspost_count_ge(ge);
    auto in_res = get_sql_filter(data, tmp);
    return in_res.at(0);
  }
  // user is filtering by crossposts? - not available via dialog
  // this filter is dumb. criteria is supposed to have one .*:.*
  // and several : to make the crosspost count.  This not part
  // of slrn specification and probably never used. not tested
  else if (criteria._text._impl_text.find(".*:.*") != std::string::npos)
  {
    StringView const v(criteria._text._impl_text);
    int const ge = std::count(v.begin(), v.end(), ':');
    FilterInfo tmp;
    tmp.set_type_crosspost_count_ge(ge);
    auto in_res = get_sql_filter(data, tmp);
    return in_res.at(0);
  }
  else
  // oh fine, then, user is doing some other damn thing with the xref
  // header.  build one for them.
  // Probably not used either
  {
    std::string sql, param;
    std::string to_test = R"SQL(
      (
        select s.host || " " || group_concat(grp.name || ":" || xr.number, " ") as xref
        from `group` as grp
        join article_group as ag on ag.group_id == grp.id and ag.article_id = article.message_id
        join article_xref as xr on xr.article_group_id = ag.id
        join server as s on xr.server_id == s.id
        group by s.host
      )
    )SQL";
    bool ok = criteria._text.create_sql_search(to_test, sql, param);
    if (ok)
        return SqlCond(sql, param);
    else
        return SqlCond();
  }
}

SqlCond HeaderFilter::get_header_sql_cond(Data const &data,
                                          FilterInfo const &criteria

) const {
  std::string sql, param, to_test, join;
  Quark header_name(criteria._header);

  if (header_name == subject) {
    to_test = "subject";
    join = "join subject on subject.id == article.subject_id";
  }

  if (header_name == from) {
    to_test = "author";
    join = "join author on author.id == article.author_id";
  }

  if (header_name == message_Id || header_name == message_ID) {
    to_test = "message_id";
  }

  if (to_test.empty()) {
    LOG4CXX_ERROR(logger, "Cannot parse header «"
                              << header_name
                              << "». Please file a bug report to "
                                 "https://gitlab.gnome.org/GNOME/pan/issues");
  } else if (criteria._text.create_sql_search(to_test, sql, param)) {
    SqlCond sc(sql, param);
    sc.join = join;
    return sc;
  }

  return SqlCond();
}

std::vector<SqlCond> HeaderFilter::get_sql_filter(
  Data const &data, FilterInfo const &criteria) const
{

  std::vector<SqlCond> res;
  ArticleCache const &cache(data.get_cache());

  switch (criteria._type)
  {
    case FilterInfo::AGGREGATE_AND:
      if (! criteria._aggregates.empty())
      {
        res.push_back(SqlCond("("));
        foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
        {
          // assume test passes if test needs body but article not cached
          if ((*it)->_needs_body)
          {
            res.push_back(SqlCond("article.cached = False"));
          }
          else
          {
            auto in_res = get_sql_filter(data, **it);
            std::for_each(in_res.begin(),
                          in_res.end(),
                          [&res](SqlCond const sc)
                          {
                            res.push_back(sc);
                          });
          }
          res.push_back(SqlCond("and"));
        }
        res.pop_back(); // remove last "and"
        res.push_back(SqlCond(")"));
      }
      break;

    case FilterInfo::AGGREGATE_OR:
      if (criteria._aggregates.empty())
      {
        res.push_back(SqlCond("True"));
      }
      else
      {
        res.push_back(SqlCond("("));
        foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
        {
          // assume test fails if test needs body but article not cached
          if ((*it)->_needs_body)
          {
            res.push_back(SqlCond("article.cached = True"));
          }
          else
          {
            auto in_res = get_sql_filter(data, **it);
            // add in_res to res
            std::for_each(in_res.begin(),
                          in_res.end(),
                          [&res](SqlCond const sc)
                          {
                            res.push_back(sc);
                          });
          }
          res.push_back(SqlCond("or"));
        }
        res.pop_back(); // remove last "or"
        res.push_back(SqlCond(")"));
      }
      break;

    case FilterInfo::IS_BINARY:
      res.push_back(SqlCond("article.part_state == 'C'"));
      break;

    case FilterInfo::IS_POSTED_BY_ME:
    {
      SqlCond sc("article.author_id in (select author_id from profile)");
      res.push_back(sc);
      break;
    }

    case FilterInfo::IS_READ:
      res.push_back(SqlCond("article.is_read == True"));
      break;

    case FilterInfo::IS_UNREAD:
      res.push_back(SqlCond("article.is_read == False"));
      break;

    case FilterInfo::BYTE_COUNT_GE:
    {
      SqlCond sc("(select sum(size) from article_part where article_id == "
                 "article.id) >= ?",
                 criteria._ge);
      res.push_back(sc);
      break;
    }

    case FilterInfo::CROSSPOST_COUNT_GE:
    {
      SqlCond sc(
        "(select count() from article_group where article_id == article.id)"
        " >= ?",
        criteria._ge);
      res.push_back(sc);
      break;
    }

    case FilterInfo::DAYS_OLD_GE:
    {
      SqlCond sc("unixepoch('now', '-' || ? || ' days') > article.time_posted",
                 criteria._ge);
      res.push_back(sc);
      break;
    }

    case FilterInfo::LINE_COUNT_GE:
      res.push_back(SqlCond("article.line_count >= ?", criteria._ge));
      break;

    case FilterInfo::TEXT:
      if (criteria._header == xref)
      {
        res.push_back(get_xref_sql_cond(data, criteria));
      }
      else if (criteria._header == newsgroups)
      {
        std::string sql_snippet, param;
        if (criteria._text.create_sql_search("grp.name", sql_snippet, param))
        {

          std::string sql(R"SQL(
            (
              select count() from `group` as grp
              join article_group as ag on ag.group_id == grp.id
              where ag.article_id == article.id
                    and )SQL"
                          + sql_snippet + ") >0 ");
          res.push_back(SqlCond(sql, param));
        }
      }
      else if (criteria._header == references)
      {
        std::string sql_snippet, param;
        if (criteria._text.create_sql_search(
              "article.`references`", sql_snippet, param))
        {
          res.push_back(SqlCond(sql_snippet, param));
        }
      }
      else if (! criteria._needs_body)
      {
        res.push_back(get_header_sql_cond(data, criteria));
      }
      // Cannot test for header value present only in cached
      // article. The whole idea of header filter is to perform the
      // search only in DB. The only alternative is to cache article
      // *in* a DB with headers stored as columns (or json, but that
      // may be slow). Let's avoid that unless people ask for it.

      //       else
      //       {
      //         if (cache.contains(article.message_id))
      //         {
      //           ArticleCache::mid_sequence_t mid(1, article.message_id);
      // #ifdef HAVE_GMIME_CRYPTO
      //           GPGDecErr err;
      //           GMimeMessage *msg = cache.get_message(mid, err);
      // #else
      //           GMimeMessage *msg = cache.get_message(mid);
      // #endif
      //           const char *hdr =
      //             g_mime_object_get_header(GMIME_OBJECT(msg),
      //             criteria._header);
      //           pass = criteria._text.test(hdr);
      //           g_object_unref(msg);
      //         }
      //         else
      //         {
      //           pass = false;
      //         }
      break;

    case FilterInfo::SCORE_GE:
      res.push_back(SqlCond(R"SQL(
          (
            select score from article_group as ag
            where ag.group_id == (select group_id from current_group limit 1)
            and article_id = article.id
          ) >= ?
        )SQL",
        criteria._ge));
      break;

    case FilterInfo::IS_CACHED:
      res.push_back(SqlCond("article.cached == True"));
      break;

    case FilterInfo::TYPE_ERR:
      assert(0 && "invalid type!");
      res.push_back(SqlCond("False"));
      break;

    case FilterInfo::TYPE_TRUE:
      res.push_back(SqlCond("True"));
      break;
  }

  if (criteria._negate)
  {
    // wrap the SQL condition in «not ( ... )»
    res.insert(res.begin(), SqlCond("not ( "));
    res.push_back(SqlCond(") "));
  }

  return res;
}
