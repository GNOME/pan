#include "pan/data/article.h"
#include "pan/data/data.h"
#include "pan/data-impl/header-rules.h"
#include "pan/general/log4cxx.h"
#include "pan/usenet-utils/scorefile.h"
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <log4cxx/logger.h>
#include <string>
#include <vector>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("header-rules"));
}

int HeaderRules::apply_rules(Data const &data) {

    return 0;
}
