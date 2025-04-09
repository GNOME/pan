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
    HeaderRules(){};

    int apply_read_rule(Data const &data, RulesInfo &rule, Quark const &group);

    int apply_rules(Data const &data, RulesInfo &rules, Quark const &group);

}; // namespace pan

}
#endif /* HEADER-RULES_H */
