


AC_DEFUN([AC_CXX_NAMESPACES],
[AC_CACHE_CHECK(whether the compiler implements namespaces,
ac_cv_cxx_namespaces,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_TRY_COMPILE([namespace Outer { namespace Inner { int i = 0; }}],
                [using namespace Outer::Inner; return i;],
 ac_cv_cxx_namespaces=yes, ac_cv_cxx_namespaces=no)
 AC_LANG_RESTORE
])
if test "$ac_cv_cxx_namespaces" = yes; then
  AC_DEFINE(HAVE_NAMESPACES,,[define if the compiler implements namespaces])
fi
])




AC_DEFUN([AC_CXX_HAVE_EXT_HASH_MAP],
[AC_CACHE_CHECK(whether the compiler has ext/hash_map,
ac_cv_cxx_have_ext_hash_map,
[AC_REQUIRE([AC_CXX_NAMESPACES])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  AC_TRY_COMPILE([#include <ext/hash_map>
#ifdef HAVE_NAMESPACES
using namespace __gnu_cxx;
#endif],[hash_map<int, int> t; return 0;],
  ac_cv_cxx_have_ext_hash_map=yes, ac_cv_cxx_have_ext_hash_map=no)
  AC_LANG_RESTORE
])
if test "$ac_cv_cxx_have_ext_hash_map" = yes; then
   AC_DEFINE(HAVE_EXT_HASH_MAP,,[define if the compiler has ext/hash_map])
fi
])
