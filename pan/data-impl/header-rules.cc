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

  }
  return count;
}
