#ifndef HEADER_FILTER_H
#define HEADER_FILTER_H

#include <SQLiteCpp/Statement.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/data/sqlcond.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>
#include <string>

namespace pan {

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
    SqlCond get_header_sql_cond(Data const &data,
                                FilterInfo const &criteria) const;
    SqlCond get_xref_sql_cond(Data const &data,
                              FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(Data const &data,
                                    FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(Data const &data,
                                    std::string const &select_str,
                                    FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(Data const &data,
                                    std::string const &select_str,
                                    SqlCond const &pre_cond,
                                    FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(
      Data const &data,
      std::function<std::string(std::string join, std::string where)> compose,
      FilterInfo const &criteria) const;
    SQLite::Statement get_sql_query(
      Data const &data,
      std::function<std::string(std::string join, std::string where)> compose,
      SqlCond const &pre_cond,
      FilterInfo const &criteria) const;

    SQLite::Statement get_sql_filter(Data const &data,
                                     Data::ShowType show_type,
                                     FilterInfo const &criteria) const;

    void score_article(Data const &data,
                       std::vector<Scorefile::Section const *> const &sections,
                       Article const &article) const;
};

} // namespace pan

#endif /* HEADER-FILTER_H */
