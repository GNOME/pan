#include "pan/data/article.h"
#include "pan/data/data.h"
#include "pan/data-impl/header-rules.h"
#include "pan/general/log4cxx.h"
#include "pan/usenet-utils/rules-info.h"
#include "pan/usenet-utils/scorefile.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <deque>
#include <log4cxx/logger.h>
#include <string>
#include <vector>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("header-rules"));
}

int HeaderRules::apply_read_rule(Data const &data,
                                 RulesInfo &rule,
                                 Quark const &group)
{
  std::string sql(R"SQL(
    update article set is_read = True
    where id in (
      select article_id from article_group as ag
      join `group` as g on g.id == ag.group_id
      where g.name = $group
        and ag.score between $lb and  $hb
    )
  )SQL");
  SQLite::Statement q(pan_db, sql);
  q.bind(1, group);
  q.bind(2, rule._lb);
  q.bind(3, rule._hb);
  return q.exec();
}

int HeaderRules::apply_delete_rule(Data const &data,
                                 RulesInfo &rule,
                                 Quark const &group)
{
  std::string sql(R"SQL(
    delete from article
    where id in (
      select article_id from article_group as ag
      join `group` as g on g.id == ag.group_id
      where g.name = $group
        and ag.score between $lb and  $hb
    )
  )SQL");
  SQLite::Statement q(pan_db, sql);
  q.bind(1, group);
  q.bind(2, rule._lb);
  q.bind(3, rule._hb);
  return q.exec();
}

int HeaderRules::apply_some_rule(Data const &data,
                                 RulesInfo &rule,
                                 Quark const &group,
                                 std::vector<Article> &setme,
                                 bool skip_read)
{
  std::string sql(R"SQL(
    select message_id from article
    join `group` as g on g.id == ag.group_id
    join  article_group as ag on ag.article_id == article.id
    where g.name = $group
      and ag.score between $lb and  $hb
  )SQL");

  if (skip_read)
  {
    sql += "and article.is_read == False";
  }

  SQLite::Statement q(pan_db, sql);
  q.bind(1, group);
  q.bind(2, rule._lb);
  q.bind(3, rule._hb);

  int count(0);
  while (q.executeStep())
  {
    Article article(group, q.getColumn(0).getText());
    setme.push_back(article);
    count++;
  }
  return count;
}

int HeaderRules::apply_rules(Data const &data,
                             RulesInfo &rules,
                             Quark const &group)
{
  int count(0);
  switch (rules._type)
  {
    case RulesInfo::AGGREGATE_RULES:
      for (int i = 0; i < rules._aggregates.size(); i++)
      {
        RulesInfo *tmp = rules._aggregates[i];
        count += apply_rules(data, *tmp, group);
      }
      break;

    case RulesInfo::MARK_READ:
      return apply_read_rule(data, rules, group);
      break;

    case RulesInfo::AUTOCACHE:
      return apply_some_rule(data, rules, group, _cached, true);
      break;

    case RulesInfo::AUTODOWNLOAD:
      return apply_some_rule(data, rules, group, _downloaded, true);
      break;

    case RulesInfo::DELETE_ARTICLE:
      // fill list of deleted article, used to cleanup my-tree. This
      // will eventually be removed when my-tree is removed.
      apply_some_rule(data, rules, group, _deleted, false);
      // remove article from DB
      return apply_delete_rule(data, rules, group);
      break;

    case pan::RulesInfo::TYPE__ERR:
      return 0;
  }
  return count;
}
