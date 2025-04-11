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

    int apply_read_rule(Data const &data, RulesInfo &rule, Quark const &group);

    int apply_rules(Data const &data, RulesInfo &rules, Quark const &group);

    std::vector<Article> _cached, _downloaded;

    // used by tests
    void reset()
    {
      _auto_cache_mark_read = false;
      _auto_dl_mark_read = false;
    }

  private:
    bool _auto_cache_mark_read, _auto_dl_mark_read;
    int apply_some_rule(Data const &data,
                        RulesInfo &rule,
                        Quark const &group,
                        std::vector<Article> &setme
                        );

  public:
    void finalize(Data &data);
}; // namespace pan

}
#endif /* HEADER-RULES_H */
