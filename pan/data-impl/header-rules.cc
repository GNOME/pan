#include "pan/data/article.h"
#include "pan/data/data.h"
#include "pan/data-impl/header-rules.h"
#include "pan/general/log4cxx.h"
#include "pan/general/time-elapsed.h"
#include "pan/tasks/queue.h"
#include "pan/usenet-utils/rules-info.h"
#include "pan/usenet-utils/scorefile.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <log4cxx/logger.h>
#include <string>
#include <vector>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("header-rules"));
}

int HeaderRules::apply_read_rule(RulesInfo &rule, Quark const &group) {
  std::string sql(R"SQL(
    update article set is_read = True
    where id in (
      select article_id from article_group as ag
      join `group` as g on g.id == ag.group_id
      where g.name = $group
        and ag.score between $lb and $hb
    )
  )SQL");
  SQLite::Statement q(pan_db, sql);
  q.bind(1, group);
  q.bind(2, rule._lb);
  q.bind(3, rule._hb);
  return q.exec();
}

int HeaderRules::apply_delete_rule(RulesInfo &rule, Quark const &group) {
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

int HeaderRules::apply_some_rule(RulesInfo &rule, Quark const &group,
                                 std::vector<Article> &setme, bool skip_read) {
  std::string sql(R"SQL(
    select message_id from article
    join `group` as g on g.id == ag.group_id
    join  article_group as ag on ag.article_id == article.id
    where g.name = $group
      and ag.score between $lb and $hb
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

int HeaderRules::apply_rules(Data &data,
                             RulesInfo &rules,
                             Quark const &group,
                             Quark const &save_path,
                             bool dry_run)
{
  TimeElapsed timer;

  int count(0);
  switch (rules._type)
  {
    case RulesInfo::AGGREGATE_RULES:
      LOG4CXX_DEBUG(logger, "aggregate apply_rules called");
      for (int i = 0; i < rules._aggregates.size(); i++)
      {
        RulesInfo *tmp = rules._aggregates[i];
        count += apply_rules(data, *tmp, group, save_path, dry_run);
      }
      break;

    case RulesInfo::MARK_READ:
      return apply_read_rule(rules, group);
      break;

    case RulesInfo::AUTOCACHE:
      return apply_some_rule(rules, group, _cached, true);
      break;

    case RulesInfo::AUTODOWNLOAD:
      return apply_some_rule(rules, group, _downloaded, true);
      break;

    case RulesInfo::DELETE_ARTICLE:
      // remove article from DB
      return apply_delete_rule(rules, group);
      break;

    case pan::RulesInfo::TYPE__ERR:
      return 0;
  }

  // top level is always an aggregate rule so the following code is
  // indeed executed.
  if (! dry_run)
  {
    // act on apply_rules results
    cache_articles(data);
    download_articles(data, save_path);
  }

  LOG4CXX_INFO(logger, "aggregate apply_rules done in " << timer.get_seconds_elapsed() << "s.");

  return count;
}

void HeaderRules ::cache_articles(Data &data)
{
  Queue *queue(data.get_queue());
  Prefs prefs(data.get_prefs());
  bool const action(prefs.get_flag("rules-autocache-mark-read", false));

  Queue::tasks_t tasks;
  ArticleCache &cache(data.get_cache());
  for (Article a : _cached)
  {
    auto t_action(action ? TaskArticle::ACTION_TRUE :
                           TaskArticle::ACTION_FALSE);
    tasks.push_back(new TaskArticle(data, data, a, cache, data, t_action));
  }

  if (! tasks.empty())
  {
    queue->add_tasks(tasks, Queue::BOTTOM);
  }
  _cached.clear();
}

void HeaderRules::download_articles(Data &data, Quark const &save_path)
{
  Queue *queue(data.get_queue());

  Queue::tasks_t tasks;
  ArticleCache &cache(data.get_cache());
  Prefs prefs(data.get_prefs());
  bool const action(prefs.get_flag("rules-auto-dl-mark-read", false));
  bool const always(prefs.get_flag("mark-downloaded-articles-read", false));

  for (Article a : _downloaded)
  {
    auto t_action(always ? TaskArticle::ALWAYS_MARK :
                  action ? TaskArticle::ACTION_TRUE :
                           TaskArticle::ACTION_FALSE);
    tasks.push_back(new TaskArticle(data,
                                    data,
                                    a,
                                    cache,
                                    data,
                                    t_action,
                                    nullptr,
                                    TaskArticle::DECODE,
                                    save_path));
  }
  if (! tasks.empty())
  {
    queue->add_tasks(tasks, Queue::BOTTOM);
  }
  _downloaded.clear();
}

