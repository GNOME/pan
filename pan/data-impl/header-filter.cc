#include "header-filter.h"
#include "pan/general/log4cxx.h"
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <log4cxx/logger.h>
#include <string>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("header-filter"));
}

SQLite::Statement HeaderFilter::get_sql_query(Data const &data,
                                              FilterInfo const &criteria) const
{
  auto sqlf = get_sql_filter(data, criteria);

  std::string sql("select message_id from article ");
  // add join clauses warning: there's a risk of adding identical join
  // clauses which may degrade perf or trigger errors
  std::for_each(sqlf.begin(),
                sqlf.end(),
                [&sql](SqlCond const sc)
                {
                  sql.append(sc.join + " ");
                });

  sql.append("where ");

  // add where clause to the SQL string
  std::for_each(sqlf.begin(),
                sqlf.end(),
                [&sql](SqlCond const sc)
                {
                  sql.append(sc.where + " ");
                });

  LOG4CXX_TRACE(logger, "SQL for header filter: " << sql);

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

std::vector<SqlCond> HeaderFilter::get_sql_filter(
  Data const &data, FilterInfo const &criteria) const
{

  std::vector<SqlCond> res;
  ArticleCache const &cache(data.get_cache());

  switch (criteria._type)
  {
      // case FilterInfo::AGGREGATE_AND:
      //   pass = true;
      //   foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
      //   {
      //     // assume test passes if test needs body but article not cached
      //     if (! (*it)->_needs_body || cache.contains(article.message_id))
      //     {
      //       if (! test_article(data, **it, group, article))
      //       {
      //         pass = false;
      //         break;
      //       }
      //     }
      //   }
      //   break;

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

      // case FilterInfo::IS_BINARY:
      //   pass = article.get_part_state() == Article::COMPLETE;
      //   break;

      // case FilterInfo::IS_POSTED_BY_ME:
      //   pass = data.has_from_header(article.get_author().to_view());
      //   break;

    case FilterInfo::IS_READ:
      res.push_back(SqlCond("article.is_read == True"));
      break;

      //     case FilterInfo::IS_UNREAD:
      //       pass = !article.is_read ();
      //       break;

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

      //     case FilterInfo::DAYS_OLD_GE:
      //       pass = (time(NULL) - article.get_time_posted()) > (criteria._ge
      //       * 86400); break;

      //     case FilterInfo::LINE_COUNT_GE:
      //       pass = article.is_line_count_ge((unsigned int)criteria._ge);
      //       break;

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
                    and )SQL" + sql_snippet
            + ") >0 ");
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
      //       else if (! criteria._needs_body)
      //       {
      //         pass = criteria._text.test(get_header(article,
      //         criteria._header));
      //       }
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

  //     case FilterInfo::SCORE_GE:
  //       pass = article.get_score() >= criteria._ge;
  //       break;

  //     case FilterInfo::IS_CACHED:
  //       pass = data.get_cache().contains(article.message_id);
  //       break;

  //     case FilterInfo::TYPE_ERR:
  //       assert(0 && "invalid type!");
  //       pass = false;
  //       break;
  }

  if (criteria._negate)
  {
    // wrap the SQL condition in «not ( ... )»
    res.insert(res.begin(), SqlCond("not ( "));
    res.push_back(SqlCond(") "));
  }

  return res;
}
