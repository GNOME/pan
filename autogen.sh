#!/bin/sh
# Run this to generate all the initial makefiles, etc.

test -n "$srcdir" || srcdir=$(dirname "$0")
test -n "$srcdir" || srcdir=.

olddir=$(pwd)

cd $srcdir

(test -f configure.ac && test -d pan) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level Pan directory"
    exit 1
}

# Allows running make dist(check) in gitlab-ci.yml
if [ ! -f pan-git.version ] ; then
    touch pan-git.version
fi

# shellcheck disable=SC2016
PKG_NAME=$(autoconf --trace 'AC_INIT:$1' configure.ac)

if [ "$#" = 0 -a "x$NOCONFIGURE" = "x" ]; then
    echo "*** WARNING: I am going to run 'configure' with no arguments." >&2
    echo "*** If you wish to pass any to it, please specify them on the" >&2
    echo "*** '$0' command line." >&2
    echo "" >&2
fi

autoreconf --verbose --force --install || exit 1

cd "$olddir"
if [ "$NOCONFIGURE" = "" ]; then
    $srcdir/configure "$@" || exit 1

    if [ "$1" = "--help" ]; then exit 0
    else
        echo "Now type 'make' to compile $PKG_NAME" || exit 1
    fi
else
    echo "Skipping configure process."
fi
