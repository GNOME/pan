AC_INIT([Pan],[0.142],[https://bugzilla.gnome.org/enter_bug.cgi?product=Pan],[pan],[http://pan.rebelbase.com/])
AC_DEFINE(VERSION_MAJOR,0,[Major part of version number])
AC_DEFINE(VERSION_MINOR,142,[Minor part of version number])
AC_DEFINE(VERSION_REVISION,0,[Revision part of version number])
AC_DEFINE(VERSION_TITLE,["He slipped to Sam a double gin"],[Release Name])
AC_PREREQ([2.64])

dnl Get git info for pan-git.version
GIT_HASH='$Format:%h$'
if test '0' \< $GIT_HASH ; then
  GIT_REMOTE=`echo '$Format:%d$' | awk -F , '{print gensub("[[\(\)]]","","g",$NF)}' 2> /dev/null`
else
  dnl git-dir is used for building outside the src tree
  GIT_HASH=`git --git-dir=$srcdir/.git log -1 --pretty=format:%h 2> /dev/null`
  if test ! -z $GIT_HASH; then
    GIT_REMOTE=`git config --get remote.origin.url 2> /dev/null`
  fi
fi
dnl Do not include protocol & user name in GIT_REMOTE
case $GIT_REMOTE in
  *://* )
  GIT_REMOTE=`echo $GIT_REMOTE | cut -d '/' -f3- 2> /dev/null`
  case $GIT_REMOTE in
    *@* )
    GIT_REMOTE=`echo $GIT_REMOTE | cut -d '@' -f2- | sed 's/\/git\//\//g' 2> /dev/null` ;;
  esac
  ;;
esac
if test ! -z $GIT_HASH; then
  if test -z $GIT_REMOTE; then
    GIT_REV="$GIT_HASH"
  else
    GIT_REV="$GIT_HASH $GIT_REMOTE"
  fi
    echo $GIT_REV > pan-git.version
  elif test -f pan-git.version ; then
    GIT_REV=`cat pan-git.version`
  else
    GIT_REV="Unknown"
fi

AC_DEFINE_UNQUOTED([GIT_REV],["$GIT_REV"],[Git hash and remote])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR(pan/general/quark.cc)

AM_INIT_AUTOMAKE([1.11.1 check-news tar-ustar dist-bzip2 foreign])
AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

dnl glib, gmime, and gtk are mandatory prerequisites.
GLIB_REQUIRED=2.17.6
GLIB_REQUIRED_FOR_DBUS=2.26.0
GMIME24_REQUIRED=2.4.0
GMIME26_REQUIRED=2.5.5
GTK_REQUIRED=2.16.0
GTK3_REQUIRED=3.0.0
GTKSPELL_REQUIRED=2.0.7
GTKSPELL3_REQUIRED=2.0.16
ENCHANT_REQUIRED=1.6.0
GNUTLS_REQUIRED=3.0.0
LIBNOTIFY_REQUIRED=0.4.1
LIBGKR_REQUIRED=3.2.0
WEBKIT_REQUIRED=1.8.1
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(GLIB_REQUIRED_FOR_DBUS)
AC_SUBST(GMIME_REQUIRED)
AC_SUBST(GTK_REQUIRED)
AC_SUBST(GTKSPELL_REQUIRED)
AC_SUBST(GNUTLS_REQUIRED)
AC_SUBST(LIBNOTIFY_REQUIRED)
AC_SUBST(LIBGKR_REQUIRED)
AC_SUBST(WEBKIT_REQUIRED)

AC_PROG_CXX
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_FUNCS([localtime_r close])
AC_CXX_HEADER_TR1_UNORDERED_SET
AC_CXX_HAVE_EXT_HASH_SET
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl for libuu
AC_CHECK_HEADERS([errno.h fcntl.h])

dnl I18n support
IT_PROG_INTLTOOL([0.40.6])
GETTEXT_PACKAGE=pan
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext package name])
panlocaledir=[${datadir}/locale]

dnl libz for xzver
PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.0],AC_DEFINE(HAVE_ZLIB,[1],[Zlib for xzver support]),[])
AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)

dnl D-Bus support
AC_ARG_WITH([dbus],
  AS_HELP_STRING([--with-dbus],[enable D-Bus support (normally: no)]),[want_dbus=$withval], [want_dbus=no])
if test "x$want_dbus" = "xyes" ; then
  AM_PATH_GLIB_2_0($GLIB_REQUIRED_FOR_DBUS,AC_DEFINE(HAVE_DBUS,[1],[D-Bus support for multiple Pan instances],exit 1,gobject gmodule gthread))
else
  AM_PATH_GLIB_2_0($GLIB_REQUIRED,,exit 1,gobject gmodule gthread)
fi

dnl Check for GMime
PKG_CHECK_MODULES([GMIME], [gmime-2.6 >= $GMIME26_REQUIRED],[have_gmime26=yes],
  [
    PKG_CHECK_MODULES([GMIME], [gmime-2.4 >= $GMIME24_REQUIRED])
    have_gmime26=no
  ]
  )

dnl Check for GMime cryptography support
AC_ARG_WITH([gmime-crypto],
  AS_HELP_STRING([--with-gmime-crypto],[enable GMime cryptography support (overrides auto-on if GMime 2.6 is found) (normally: yes)]),
  [want_gmime_crypto=$withval], [want_gmime_crypto=yes])

if test "x$want_gmime_crypto" = "xyes" ; then
if test "x$have_gmime26" = "xyes" ; then
    AC_DEFINE(HAVE_GMIME_CRYPTO,[1],[cryptography (PGP) support with GMime 2.6])
  fi
fi

dnl GtkSpell support
gtkspell_msg=no
AC_ARG_WITH([gtkspell],
  AS_HELP_STRING([--with-gtkspell],[enable GtkSpell support (normally: yes)]), [want_gtkspell=$withval], [want_gtkspell=yes])

dnl GTK+ 3, GtkSpell 3, and Enchant support
gtk_msg=no
AC_ARG_WITH([gtk3],
  AS_HELP_STRING([--with-gtk3],[enable GTK+ 3 support (normally: no)]), [want_gtk3=$withval], [want_gtk3=no])
if test "x$want_gtk3" = "xyes" ; then
  PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= $GTK3_REQUIRED],
                    [gtk_msg="yes >= $GTK3_REQUIRED"
                    AC_DEFINE(HAVE_GTK,[1],[GTK+ 3 support])]
                    )
  if test "x$want_gtkspell" = "xyes" ; then
    PKG_CHECK_MODULES([GTKSPELL], [gtkspell3-3.0 >= $GTKSPELL3_REQUIRED enchant >= $ENCHANT_REQUIRED],
                      [
                      gtkspell_msg=yes
                      AC_DEFINE(HAVE_GTKSPELL,[1],[GtkSpell support for spellchecking])
                      AC_DEFINE(GTKSPELL_VERSION,[3],[GtkSpell API version])
                      ],
                      [
                      gtkspell_msg=no
                      AC_MSG_RESULT(no)
                      ])
  fi
else
   if test "x$want_gtkspell" = "xyes" ; then
   PKG_CHECK_MODULES([GTKSPELL], [gtkspell-2.0 >= $GTKSPELL_REQUIRED enchant >= $ENCHANT_REQUIRED],
                      [
                      gtkspell_msg=yes
                      AC_DEFINE(HAVE_GTKSPELL,[1],[GtkSpell support for spellchecking])
                      AC_DEFINE(GTKSPELL_VERSION,[2],[GtkSpell API version])
                      ],
                      [
                      gtkspell_msg=no
                      AC_MSG_RESULT(no)
                      ])
   fi
   AM_PATH_GTK_2_0($GTK_REQUIRED,,exit 1,gthread)
fi

AC_SUBST(ENCHANT_CFLAGS)
AC_SUBST(ENCHANT_LIBS)

dnl WebKitGTK+ support
AC_ARG_WITH([webkit],
  AS_HELP_STRING([--with-webkit],[enable WebKitGTK+ support (normally: no)]), [want_webkit=$withval], [want_webkit=no])
if test "x$want_gtk3" = "xyes" ; then
  if test "x$want_webkit" = "xyes" ; then
    PKG_CHECK_MODULES([WEBKITGTK],[webkitgtk-3.0 >= $WEBKIT_REQUIRED],
    [
    AC_DEFINE(HAVE_WEBKIT,[1],[WebKit support (3.0) for embedded HTML])
    ],
    [
    AC_MSG_RESULT(no)
    ])
  fi
else
  if test "x$want_webkit" = "xyes" ; then
    PKG_CHECK_MODULES([WEBKITGTK], [webkitgtk-1.0 >= $WEBKIT_REQUIRED],
    [
    AC_DEFINE(HAVE_WEBKIT,[1],[WebKit support (1.0) for embedded HTML])
    ],
    [
    AC_MSG_RESULT(no)
    ])
  fi
fi

dnl GnuTLS support
gnutls_msg=no
AC_ARG_WITH([gnutls],
  AS_HELP_STRING([--with-gnutls],[enable GnuTLS support (normally: no)]), [want_gnutls=$withval], [want_gnutls=no])
if test "x$want_gnutls" = "xyes" ; then
  PKG_CHECK_MODULES([GNUTLS],[gnutls >= $GNUTLS_REQUIRED],
  [
  GNUTLS_VER=`pkg-config --modversion gnutls`
  gnutls_msg="yes ($GNUTLS_VER)"
  AC_DEFINE(HAVE_GNUTLS,[1],[GnuTLS support])],
  [gnutls_msg=no
  AC_MSG_RESULT(no)])
  AC_SUBST([GNUTLS_CFLAGS])
  AC_SUBST([GNUTLS_LIBS])
fi

dnl Check for libnotify if user-enabled for popup notifications
AC_ARG_ENABLE([libnotify],
  AS_HELP_STRING([--enable-libnotify],[enable libnotify support (normally: no)]),[enable_libnotify=$enableval],[enable_libnotify=no])
if test "x$enable_libnotify" = "xyes" ; then
  PKG_CHECK_MODULES([LIBNOTIFY],[libnotify >= $LIBNOTIFY_REQUIRED],[HAVE_LIBNOTIFY="yes"],[HAVE_LIBNOTIFY="no"])
  AC_SUBST([LIBNOTIFY_CFLAGS])
  AC_SUBST([LIBNOTIFY_LIBS])
  if test "x$HAVE_LIBNOTIFY" = "xyes"; then
    AC_DEFINE([HAVE_LIBNOTIFY],[1],[libnotify support for popup notifications (experimental)])
  fi
fi

dnl Check for GNOME Keyring if user-enabled for password storage
AC_ARG_ENABLE([gkr],
  AS_HELP_STRING([--enable-gkr],[enable GNOME Keyring support (normally: no)]),[enable_gkr=$enableval],[enable_gkr=no])
if test "x$enable_gkr" = "xyes" ; then
  PKG_CHECK_MODULES([LIBGNOME_KEYRING_1],[gnome-keyring-1 >= $LIBGKR_REQUIRED],[HAVE_GKR="yes"],[HAVE_GKR="no"])
  AC_SUBST([LIBGNOME_KEYRING_1_CFLAGS])
  AC_SUBST([LIBGNOME_KEYRING_1_LIBS])
  if test "x$HAVE_GKR" = "xyes"; then
    AC_DEFINE([HAVE_GKR],[1],[GNOME Keyring support for password storage])
  fi
fi

dnl User manual infrastructure
AC_ARG_WITH([yelp-tools],
  AS_HELP_STRING([--with-yelp-tools],[enable yelp-tools support for documentation (normally: yes)]),
  [want_yelp_tools=$withval],[want_yelp_tools=yes])
AC_ARG_ENABLE([manual],
  AS_HELP_STRING([--enable-manual],[build in user manual (normally: no)]),
  [enable_manual=$enableval], [enable_manual=no])
if test "x$want_yelp_tools" = "xyes" ; then
  YELP_HELP_INIT
  if test "x$enable_manual" = "xyes" ; then
    AC_DEFINE(HAVE_MANUAL,[1], [user manual for Pan])
    AM_CONDITIONAL(ENABLE_MANUAL, true)
  else
    AM_CONDITIONAL(ENABLE_MANUAL, false)
  fi
else
  AM_CONDITIONAL(ENABLE_MANUAL, false)
fi

dnl Check to see if strftime supports the use of %l and %k
AC_MSG_CHECKING(for %l and %k support in strftime)
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <string.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv) {
  char buf[10];
  time_t rawtime = time(0);
  struct tm *timeinfo = localtime (&rawtime);
  strftime(buf, 10, "%l %k", timeinfo);
  exit (strstr(buf, "l") || strstr(buf, "k") || !strcmp(buf," ") || !strlen(buf));
}]])],[
AC_DEFINE(HAVE_LKSTRFTIME, 1, [strftime supports use of l and k])
ac_cv_lkstrftime=yes
],[ac_cv_lkstrftime=no],[ac_cv_lkstrftime=no])
AC_MSG_RESULT($ac_cv_lkstrftime)

AC_CANONICAL_HOST

AC_DEFINE_UNQUOTED([PLATFORM_INFO],["$host"],[uname cpu-vendor-platformbuild])

dnl Windows-specific stuff
win32="no"
case $host_os in

	*darwin*|*rhapsody*)
	AC_DEFINE(G_OS_DARWIN,[1],[Darwin])
	;;

	*cygwin|*mingw32*)
	CXXFLAGS="$CXXFLAGS -mms-bitfields -mwin32"
	CPPFLAGS="$CPPFLAGS -DWIN32_LEAN_AND_MEAN"
	LIBS="$LIBS -lshell32 -lws2_32 -lpthread"
	panlocaledir="locale"

	if test -z "$host_alias"; then
		hostaliaswindres=
	else
		hostaliaswindres="$host_alias-windres";
	fi
	AC_PATH_PROGS([WINDRES], [windres $hostaliaswindres $host_os-windres])
	if test "x$WINDRES" = "x"; then
		AC_MSG_ERROR([windres could not be found within your path.])
	fi
	AC_SUBST(WINDRES)
	win32="yes"
	;;
esac
AM_CONDITIONAL([HAVE_WIN32],[test "$win32" = "yes"])

dnl Build the output files
AC_SUBST(panlocaledir)
AC_CONFIG_FILES([Makefile
                 README.windows
                 uulib/Makefile
                 pan/Makefile
                 pan/general/Makefile
                 pan/usenet-utils/Makefile
                 pan/data/Makefile
                 pan/tasks/Makefile
                 pan/icons/Makefile
                 pan/data-impl/Makefile
                 pan/gui/Makefile
                 po/Makefile.in
                 help/Makefile])

AC_OUTPUT

echo "

Configuration:

        Prefix:                 ${prefix}
        Source code location:   ${srcdir}
        Compiler:               ${CXX}
        With D-Bus:             ${want_dbus}
        With GMime 2.6+:        ${have_gmime26}
        With GMime crypto:      ${want_gmime_crypto}
        With GtkSpell:          ${gtkspell_msg}
        With GTK+ 3:            ${gtk_msg}
        With WebKitGTK+:        ${want_webkit}
        With GnuTLS:            ${gnutls_msg}
        With libnotify:         ${enable_libnotify}
        With GNOME Keyring:     ${enable_gkr}
        With yelp-tools:        ${want_yelp_tools}
        With user manual:       ${enable_manual}
"
