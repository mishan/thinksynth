dnl $Id: configure.ac,v 1.22 2003/04/27 00:02:50 joshk Exp $

AC_INIT

dnl Don't let ancient Autoconfs touch this
AC_PREREQ(2.5)

if test "$prefix" = "NONE"; then
	prefix=/usr/local
fi

AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_CANONICAL_TARGET

PACKAGE="thinksynth"
VERSION="0.1"
AM_INIT_AUTOMAKE("$PACKAGE", "$VERSION")

AC_PROG_CXX([$CXX g++ c++])
AC_PROG_CC([$CC gcc cc egcs])
AC_PROG_CXXCPP

LEX_LANG=CXX
YACC_LANG=CXX

AM_PROG_LEX

if test "`basename $LEX`" = "missing"; then
	AC_MSG_ERROR([A lex implementation is required to build this program.])
fi

AC_PROG_YACC

dnl XXX this is a hack until autoconf gets fixed
if test "$YACC" = "yacc"; then
	if test ! -x $YACC; then
		YACC=:
	fi
fi

if test "$YACC" = ":"; then
	AC_MSG_ERROR([A yacc implementation is required to build this program.])
fi

dnl The autoconf macro doesn't realize that it NEEDS -d to produce
dnl the appropriate headers, so add this stupid hack.

YACC="$YACC -d"

AC_ISC_POSIX
AC_HEADER_STDC
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN

AC_TYPE_SIGNAL
AC_TYPE_SIZE_T

AC_PATH_PROG(sedpath, sed)
if test "_$sedpath" = _; then
	AC_MSG_ERROR("Cannot find sed: I need it\!")
fi

CXXFLAGS="-Wall -pedantic -rdynamic -ffast-math"

AC_CHECK_HEADER([sys/soundcard.h],,[AC_MSG_ERROR([You're missing sys/soundcard.h. We need this for sound.])])

AC_CHECK_HEADER([dlfcn.h],,[AC_MSG_ERROR([You're missing dlfcn.h, we need this for dlopen.])])

dnl Linux has libdl
AC_CHECK_LIB(dl, dlopen, [LIBS="$LIBS -ldl"],

dnl Others may have it in libc_r
[AC_CHECK_LIB(c_r, dlopen,
[
	LIBS="$LIBS -lc_r"
	lcr_for_dl=yes
],

dnl We need at least one of these
[AC_MSG_ERROR([No dlopen implementation found on your system. Sorry.])])])

dnl Be smart this time round
if test "$lcr_for_dl" = yes; then
	AC_CHECK_LIB(c_r, dlerror, [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])],)
else
	AC_CHECK_LIB(dl, dlerror, [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])],)
fi

AC_PATH_PROG(unamepath, uname)
if test "_$unamepath" = _; then
	system="unknown"
else
	AC_MSG_CHECKING(system type)
	system=`$unamepath -s`
	AC_MSG_RESULT($system)
	if test "$system" = "Linux"; then
		AC_DEFINE([USING_LINUX], 1, [Define if using Linux])
	fi
	if test "$system" = "FreeBSD"; then
		AC_DEFINE([USING_FREEBSD], 1, [Define if using FreeBSD])
		CXXFLAGS="$CXXFLAGS -Wno-long-long"
	fi
	if test "$system" = "Darwin"; then
	        CXXFLAGS="$CXXFLAGS -traditional-cpp"
	        AC_DEFINE([USING_DARWIN], 1, [Define if using Darwin])
	fi
fi

AC_ARG_ENABLE(debug,
AC_HELP_STRING([--disable-debug],[print debug messages to stdout (default on)]),

dnl Debug is disabled:
[
	CXXFLAGS="$CXXFLAGS -O2"
	debug=no
],

dnl Debug is enabled:
[
	AC_DEFINE([USE_DEBUG], 1, [Define if you want debugging enabled])
	CXXFLAGS="$CXXFLAGS -g -ggdb"
	debug=yes
])

AC_ARG_ENABLE(efence,
	[AC_HELP_STRING([--enable-efence],[Enable ElectricFence in the build])],
	[AC_CHECK_LIB(efence, malloc,
		[LIBS="$LIBS -lefence"],
		[AC_MSG_WARN([--enable-efence specified but could not link to it - disabling])])])
	
AC_ARG_ENABLE(,,,)

AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [The prefix where thinksynth is installed])

AC_SUBST(CXXFLAGS)
AC_SUBST(LIBS)

dnl How chomo!
AC_SUBST_DIR(libdir)
AC_SUBST_DIR(datadir)
AC_SUBST_DIR(bindir)

plugin_path=${libdir}/${PACKAGE}/plugins/
AC_DEFINE_UNQUOTED([PLUGIN_PATH], "$plugin_path", [Path to plugins.])
AC_SUBST(plugin_path)

AC_OUTPUT([Makefile src/Makefile plugins/plugin-common.inc dsp/Makefile])

echo
echo $PACKAGE $VERSION
echo
echo "compiling for system ....:" $system
echo "debug ...................:" $debug
echo
echo The binary will be installed in $bindir
echo Data will be installed in       $datadir/$PACKAGE
echo Plugins will be installed in    $plugin_path
echo
echo "Now run 'make' to build $PACKAGE"
