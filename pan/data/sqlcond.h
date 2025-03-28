#ifndef SQLCOND_H
#define SQLCOND_H

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

} // namespace pan

#endif /* SQLCOND_H */
