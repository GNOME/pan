#ifndef HEADER_FILTER_H
#define HEADER_FILTER_H

#include <SQLiteCpp/Statement.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>
#include <string>

namespace pan {

class SqlCond
{
  public:
    enum cond_type
    {
      NO_PARAM,
      STR_PARAM,
      BOOL_PARAM,
      INT_PARAM
    };

    std::string join;
    std::string where;
    std::string str_param;
    int int_param;
    bool bool_param;
    cond_type type;

    SqlCond(std::string const where, std::string const _p) :
      where(where),
      str_param(_p),
      type(STR_PARAM)
    {
    }

    SqlCond(std::string const where, char const *_p) :
      where(where),
      str_param(std::string(_p)),
      type(STR_PARAM)
    {
    }

    SqlCond(std::string const where, long const _p) :
      where(where),
      int_param(_p),
      type(INT_PARAM)
    {
    }

    SqlCond(std::string const where, bool const _p) :
      where(where),
      bool_param(_p),
      type(BOOL_PARAM)
    {
    }

    SqlCond(std::string const where) :
      where(where),
      type(NO_PARAM)
    {
    }

    SqlCond()
    {
    }
};

class HeaderFilter
{
  private:
    Quark const subject;
    Quark const from;
    Quark const xref;
    Quark const references;
    Quark const newsgroups;
    Quark const message_Id;
    Quark const message_ID;

  public:
    HeaderFilter() :
      subject("Subject"),
      from("From"),
      xref("Xref"),
      references("References"),
      newsgroups("Newsgroups"),
      message_Id("Message-Id"),
      message_ID("Message-ID")
    {
    }

    std::vector<SqlCond> get_sql_filter(Data const &data,
                                        FilterInfo const &criteria) const;
    SqlCond get_xref_sql_cond(Data const &data,
                              FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(Data const &data,
                                    FilterInfo const &criteria) const;
};

} // namespace pan

#endif /* HEADER-FILTER_H */
