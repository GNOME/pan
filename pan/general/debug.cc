#include <config.h>
#include "debug.h"

namespace pan
{
  bool _debug_flag = false;
  bool _debug_verbose_flag = false;
  bool _verbose_flag = false;
  std::ofstream _dbg_file;

  bool _dbg_ssl = false;

}
