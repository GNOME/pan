AC_INIT([Pan],[0.154],[https://gitlab.gnome.org/GNOME/pan/issues],[pan],[https://gitlab.gnome.org/GNOME/pan])
AC_DEFINE(VERSION_MAJOR,0,[Major part of version number])
AC_DEFINE(VERSION_MINOR,154,[Minor part of version number])
AC_DEFINE(VERSION_REVISION,0,[Revision part of version number])
AC_DEFINE(VERSION_TITLE,["Izium"],[Release Name])
AC_PREREQ([2.71])

dnl Get git info for pan-git.version
GIT_HASH='$Format:%h$'
if test ! '0' \< $GIT_HASH ; then
  dnl git-dir is used for building outside the src tree
  GIT_HASH=`git --git-dir=$srcdir/.git log -1 --pretty=format:%h 2> /dev/null`
  if test -n $GIT_HASH; then
    GIT_REMOTE=`git config --get remote.origin.url 2> /dev/null`
  fi
fi
if test -n $GIT_HASH; then
  if test -z $GIT_REMOTE; then
    GIT_REV="$GIT_HASH"
  else
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
    GIT_REV="$GIT_HASH $GIT_REMOTE"
  fi
  echo $GIT_REV > pan-git.version
  elif test -s pan-git.version ; then
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
GMIME30_REQUIRED=3.0.0
GTK_REQUIRED=2.24.0
GTK3_REQUIRED=3.0.0
GTKSPELL_REQUIRED=2.0.7
GTKSPELL3_REQUIRED=2.0.16
ENCHANT2_REQUIRED=2.2.3
GNUTLS_REQUIRED=3.0.0
LIBNOTIFY_REQUIRED=0.4.1
LIBGKR_REQUIRED=3.2.0
LIBSECRET_REQUIRED=0.16
GCR_REQUIRED=3.20
WEBKIT_REQUIRED=1.8.1
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(GLIB_REQUIRED_FOR_DBUS)
AC_SUBST(GMIME_REQUIRED)
AC_SUBST(GTK_REQUIRED)
AC_SUBST(GTKSPELL_REQUIRED)
AC_SUBST(GNUTLS_REQUIRED)
AC_SUBST(LIBNOTIFY_REQUIRED)
AC_SUBST(LIBGKR_REQUIRED)
AC_SUBST(LIBSECRET_REQUIRED)
AC_SUBST(GCR_REQUIRED)
AC_SUBST(WEBKIT_REQUIRED)

AC_PROG_CXX

AC_CHECK_FUNCS([localtime_r close])
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl for libuu
AC_CHECK_HEADERS([errno.h fcntl.h])

dnl I18n support
GETTEXT_PACKAGE=pan
AC_SUBST(GETTEXT_PACKAGE)
AM_GNU_GETTEXT_VERSION([0.21])
AM_GNU_GETTEXT([external])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
panlocaledir=[${datadir}/locale]

dnl check if libiconv library needs to be linked
AM_ICONV

dnl libz for xzver
PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.0],AC_DEFINE(HAVE_ZLIB,[1],[Zlib for xzver support]),[])
AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)

dnl D-Bus support
AC_ARG_WITH([dbus],
  AS_HELP_STRING([--with-dbus],[enable D-Bus support (normally: no)]), [want_dbus=$withval], [want_dbus=no])
if test "x$want_dbus" = "xyes" ; then
  AM_PATH_GLIB_2_0($GLIB_REQUIRED_FOR_DBUS,AC_DEFINE(HAVE_DBUS,[1],[D-Bus support for multiple Pan instances],exit 1,gobject gmodule gthread))
else
  AM_PATH_GLIB_2_0($GLIB_REQUIRED,,exit 1,gobject gmodule gthread)
fi
dnl Silence deprecation warnings for things deprecated before 2.26,
dnl and generally set the target behaviour to be like GLib 2.26.
dnl In principle this should match $GLIB_REQUIRED, but 2.26 is the oldest
dnl version for which the macros exist (and has been unsupported since
dnl around 2011 in any case).
GLIB_CFLAGS="$GLIB_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_26"
dnl Highest version number from which new features are allowed to be used,
dnl even within a GLIB_CHECK_VERSION check. GLib will produce compile-time
dnl warnings when features of a newer GLib version are used.
GLIB_CFLAGS="$GLIB_CFLAGS -DGLIB_VERSION_MAX_ALLOWED=G_ENCODE_VERSION\\(2,36\\)"

dnl Check for GMime
PKG_CHECK_MODULES([GMIME], [gmime-3.0 >= $GMIME30_REQUIRED])

dnl Check for GMime cryptography support
AC_ARG_WITH([gmime-crypto],
  AS_HELP_STRING([--with-gmime-crypto],[enable GMime cryptography support (normally: yes)]),
  [want_gmime_crypto=$withval], [want_gmime_crypto=yes])

if test "x$want_gmime_crypto" = "xyes" ; then
  AC_DEFINE(HAVE_GMIME_CRYPTO,[1],[cryptography (PGP) support with GMime])
fi

dnl GtkSpell support
gtkspell_msg=no
AC_ARG_WITH([gtkspell],
  AS_HELP_STRING([--with-gtkspell],[enable GtkSpell support (normally: yes)]), [want_gtkspell=$withval], [want_gtkspell=yes])

dnl GTK 3, GtkSpell 3, and Enchant support
gtk_msg=no
PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= $GTK3_REQUIRED],
                    [GTK_VER=`pkg-config --modversion gtk+-3.0`
                    gtk_msg="yes, GTK3 ($GTK_VER)"
                    AC_DEFINE(HAVE_GTK,[1],[GTK 3 support])]
                    )

if test "x$want_gtkspell" = "xyes" ; then
  PKG_CHECK_MODULES([GTKSPELL], [gtkspell3-3.0 >= $GTKSPELL3_REQUIRED enchant-2 >= $ENCHANT2_REQUIRED],
                      [GTKSPELL_VER=`pkg-config --modversion gtkspell3-3.0`
                      ENCHANT_VER=`pkg-config --modversion enchant-2`
                      gtkspell_msg="yes (gtkspell: $GTKSPELL_VER, enchant: $ENCHANT_VER)"
                      AC_DEFINE(HAVE_GTKSPELL,[1],[GtkSpell support for spellchecking])
                      AC_DEFINE(GTKSPELL_VERSION,[3],[GtkSpell API version])]
                      )
fi

AC_SUBST(ENCHANT_CFLAGS)
AC_SUBST(ENCHANT_LIBS)

dnl WebKitGTK support
AC_ARG_WITH([webkit],
  AS_HELP_STRING([--with-webkit],[enable WebKitGTK support (experimental, normally: no)]), [want_webkit=$withval], [want_webkit=no])
if test "x$want_webkit" = "xyes" ; then
  PKG_CHECK_MODULES([WEBKITGTK],[webkitgtk-3.0 >= $WEBKIT_REQUIRED],
  [
  AC_DEFINE(HAVE_WEBKIT,[1],[WebKit support (3.0) for embedded HTML])
  ],
  [
  AC_MSG_RESULT(no)
  ])
fi

dnl GnuTLS support
gnutls_msg=no
AC_ARG_WITH([gnutls],
  AS_HELP_STRING([--with-gnutls],[enable GnuTLS support (normally: no)]), [want_gnutls=$withval], [want_gnutls=no])
if test "x$want_gnutls" = "xyes" ; then
  PKG_CHECK_MODULES([GNUTLS],[gnutls >= $GNUTLS_REQUIRED],
  [GNUTLS_VER=`pkg-config --modversion gnutls`
  gnutls_msg="yes ($GNUTLS_VER)"
  AC_DEFINE(HAVE_GNUTLS,[1],[GnuTLS support])],
  [gnutls_msg=no
  AC_MSG_RESULT(no)])
  AC_SUBST([GNUTLS_CFLAGS])
  AC_SUBST([GNUTLS_LIBS])
fi

dnl Check for libnotify if user-enabled for popup notifications
libnotify_msg=no
AC_ARG_ENABLE([libnotify],
  AS_HELP_STRING([--enable-libnotify],[enable libnotify support (normally: no)]),[enable_libnotify=$enableval],[enable_libnotify=no])
if test "x$enable_libnotify" = "xyes" ; then
  PKG_CHECK_MODULES([LIBNOTIFY],[libnotify >= $LIBNOTIFY_REQUIRED],
  [LIBNOTIFY_VER=`pkg-config --modversion libnotify`
  libnotify_msg="yes ($LIBNOTIFY_VER)"
  AC_DEFINE([HAVE_LIBNOTIFY],[1],[libnotify support for popup notifications])],
  [libnotify_msg=no
  AC_MSG_RESULT(no)])
  AC_SUBST([LIBNOTIFY_CFLAGS])
  AC_SUBST([LIBNOTIFY_LIBS])
fi

dnl Check for libsecret and gcr if user-enabled for password storage
gkr_msg=no
AC_ARG_ENABLE([gkr],
  AS_HELP_STRING([--enable-gkr],[enable GNOME Keyring or libsecret support (normally: no)]),[enable_gkr=$enableval],[enable_gkr=no])
if test "x$enable_gkr" = "xyes" ; then
  PKG_CHECK_MODULES([LIBGNOME_KEYRING_1],[libsecret-1 >= $LIBSECRET_REQUIRED gcr-3 >= $GCR_REQUIRED],
  [LIBSECRET_VER=`pkg-config --modversion libsecret-1 gcr-3 | xargs`
  gkr_msg="yes ($LIBSECRET_VER)"
  AC_DEFINE(HAVE_GKR,[1],[GNOME Keyring or libsecret support for password storage])],
  [gkr_msg=no
  AC_MSG_RESULT(no)])

  AC_SUBST([LIBGNOME_KEYRING_1_CFLAGS])
  AC_SUBST([LIBGNOME_KEYRING_1_LIBS])
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
	CXXFLAGS="$CXXFLAGS -mms-bitfields"
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

dnl Sanity checking
CXXFLAGS="$CXXFLAGS -Wreorder -Wzero-as-null-pointer-constant"

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
        With GMime crypto:      ${want_gmime_crypto}
        With GtkSpell:          ${gtkspell_msg}
        With GTK:               ${gtk_msg}
        With WebKitGTK:         ${want_webkit}
        With GnuTLS:            ${gnutls_msg}
        With libnotify:         ${libnotify_msg}
        With password storage:  ${gkr_msg}
        With yelp-tools:        ${want_yelp_tools}
        With user manual:       ${enable_manual}
"
