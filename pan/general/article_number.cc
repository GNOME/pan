#include "article_number.h"

#include <pan/general/string-view.h>

#include <glib.h>

#include <ostream>

namespace pan {

Article_Count::Article_Count(StringView const &str)
{
  if (str.empty())
  {
    val_ = 0;
  }
  else
  {
    char *end;
    val_ = g_ascii_strtoull(str.str, &end, 10);
    //Error checking?
    //*end != 0
    //val = max and errno set to E_RANGE
  }
}

std::ostream &operator<<(std::ostream &os, Article_Count a)
{
  return os << static_cast<Article_Count::type>(a);
}

Article_Number::Article_Number(StringView const &str)
{
  if (str.empty())
  {
    val_ = 0;
  }
  else
  {
    char *end;
    val_ = g_ascii_strtoull(str.str, &end, 10);
    //Error checking?
    //*end != 0
    //val = max and errno set to E_RANGE
  }
}

std::ostream &operator<<(std::ostream &os, Article_Number a)
{
  return os << static_cast<Article_Number::type>(a);
}

}
