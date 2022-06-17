
dnl Benjamin Kosnik <bkoz@redhat.com> 
dnl Last Modified 2008-04-17 
AC_DEFUN([AC_CXX_HEADER_TR1_UNORDERED_SET], [
  AC_CACHE_CHECK(for tr1/unordered_set,
  ac_cv_cxx_tr1_unordered_set,
  [AC_LANG_SAVE
  AC_LANG([C++])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <tr1/unordered_set>]], [[using std::tr1::unordered_set;]])],[ac_cv_cxx_tr1_unordered_set=yes],[ac_cv_cxx_tr1_unordered_set=no])
  AC_LANG_RESTORE
  ])
  if test "$ac_cv_cxx_tr1_unordered_set" = yes; then
    AC_DEFINE(HAVE_TR1_UNORDERED_SET,,[Define if tr1/unordered_set is present. ])
  fi
])



AC_DEFUN([AC_CXX_NAMESPACES],
[AC_CACHE_CHECK(whether the compiler implements namespaces,
ac_cv_cxx_namespaces,
[AC_LANG_SAVE
 AC_LANG([C++])
 AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[namespace Outer { namespace Inner { int i = 0; }}]], [[using namespace Outer::Inner; return i;]])],[ac_cv_cxx_namespaces=yes],[ac_cv_cxx_namespaces=no])
 AC_LANG_RESTORE
])
if test "$ac_cv_cxx_namespaces" = yes; then
  AC_DEFINE(HAVE_NAMESPACES,,[define if the compiler implements namespaces])
fi
])




AC_DEFUN([AC_CXX_HAVE_EXT_HASH_SET],
[AC_CACHE_CHECK(whether the compiler has ext/hash_set,
ac_cv_cxx_have_ext_hash_set,
[AC_REQUIRE([AC_CXX_NAMESPACES])
  AC_LANG_SAVE
  AC_LANG([C++])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <ext/hash_set>
#ifdef HAVE_NAMESPACES
using namespace __gnu_cxx;
#endif]], [[hash_set<int, int> t; return 0;]])],[ac_cv_cxx_have_ext_hash_set=yes],[ac_cv_cxx_have_ext_hash_set=no])
  AC_LANG_RESTORE
])
if test "$ac_cv_cxx_have_ext_hash_set" = yes; then
   AC_DEFINE(HAVE_EXT_HASH_SET,,[define if the compiler has ext/hash_set])
fi
])
