#ifndef HEADER_RULES_H
#define HEADER_RULES_H

#include <SQLiteCpp/Statement.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/data/sqlcond.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>
#include <vector>

namespace pan {

    // used to perform actions (like cache, download, delete) on
    // article depending on their score (set in pref->action menu)
class HeaderRules
{
  public:
    HeaderRules() :
      _auto_cache_mark_read(false),
      _auto_dl_mark_read(false) {};

    HeaderRules(bool cache, bool dl) :
      _auto_cache_mark_read(cache),
      _auto_dl_mark_read(dl) {};

    // Mark articles as read in DB depending on their score. Score
    // limits are contained in rule parameter. Returns the number of
    // marked articles.
    int apply_read_rule(RulesInfo &rule, Quark const &group);

    // Delete articles from DB depending on their score. Score limits
    // are contained in rule parameter. Returns the number of marked
    // articles.
    int apply_delete_rule(RulesInfo &rule,
                          Quark const &group);

    // apply rules and trigger actions. If dry_run is set, actions are
    // performed only in DB and actual download or caching is
    // disabled.
    int apply_rules(Data &data,
                    RulesInfo &rules,
                    Quark const &group,
                    Quark const &save_path,
                    bool dry_run = false);

    std::vector<Article> _cached, _downloaded;

    // used by tests
    void reset()
    {
      _auto_cache_mark_read = false;
      _auto_dl_mark_read = false;
    }

  private:
    bool _auto_cache_mark_read, _auto_dl_mark_read;
    // Store articles in setme parameter depending on their
    // score. Score limits are contained in rule parameter. Read
    // article can be excluded from the result when skip_read is
    // true. Returns the number of stored articles.
    int append_articles_affected_by_rule(RulesInfo &rule,
                        Quark const &group,
                        std::vector<Article> &setme,
                        bool skip_read);

  public:
    void cache_articles(Data &data);
    void download_articles(Data &data, Quark const &save_path);

}; // namespace pan

}
#endif /* HEADER-RULES_H */
