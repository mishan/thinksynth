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
AC_PROG_YACC

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

AC_CHECK_HEADER([sys/soundcard.h],,[AC_MSG_ERROR([You're missing sys/soundcard.h. Is your C includes install complete?])])
AC_CHECK_HEADER([dlfcn.h],,[AC_MSG_ERROR([You're missing libdl includes. Is your C includes install copmlete?])])
AC_CHECK_LIB(dl, dlopen, [LIBS="$LIBS -ldl"], [AC_MSG_ERROR([You need libdl to compile and use this program!])])
AC_CHECK_LIB(dl, dlerror, [AC_DEFINE(HAVE_DLERROR)],)

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
	CXXFLAGS="$CXXFLAGS -g"
	debug=yes
])

AC_ARG_ENABLE(,,,)

AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [The prefix where thinksynth is installed])

AC_SUBST(CXXFLAGS)
AC_SUBST(LIBS)

dnl ENV_SOURCES=`find plugins/env/ -iname \"*.c\"`
dnl FILT_SOURCES=`find plugins/filt/ -iname \"*.c\"`
dnl MATH_SOURCES=`find plugins/math/ -iname \"*.c\"`
dnl MISC_SOURCES=`find plugins/misc/ -iname \"*.c\"`
dnl MIXER_SOURCES=`find plugins/mixer/ -iname \"*.c\"`
dnl OSC_SOURCES=`find plugins/osc/ -iname \"*.c\"`

dnl AC_SUBST(ENV_SOURCES)
dnl AC_SUBST(FILT_SOURCES)
dnl AC_SUBST(MATH_SOURCES)
dnl AC_SUBST(MISC_SOURCES)
dnl AC_SUBST(MIXER_SOURCES)
dnl AC_SUBST(OSC_SOURCES)

dnl plugins/env/Makefile
dnl plugins/filt/Makefile
dnl plugins/math/Makefile
dnl plugins/misc/Makefile
dnl plugins/mixer/Makefile

AC_SUBST_DIR(libdir)

dnl XXX let's change this later
plugin_path=${libdir}/${PACKAGE}/plugins/

AC_DEFINE_UNQUOTED([PLUGIN_PATH], "$plugin_path", [Path to plugins.])

AC_SUBST(plugin_path)

AC_OUTPUT([Makefile src/Makefile plugins/plugin-common.inc])

echo
echo $PACKAGE $VERSION
echo
echo "compiling for system ....:" $system
echo "debug ...................:" $debug
echo
echo The binary will be installed in $prefix/bin
echo Data will be installed in       $prefix/share/$PACKAGE
echo Plugins will be installed in    $plugin_path
echo
echo Now run \'make\' to build $PACKAGE
