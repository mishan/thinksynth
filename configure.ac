AC_INIT(src/main.cpp)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

PACKAGE="thinksynth"
VERSION="0.1"
AM_INIT_AUTOMAKE("$PACKAGE", "$VERSION")

#AC_PROG_CC
AC_PROG_CXX
AC_PROG_CXXCPP
AC_CANONICAL_TARGET

LEX_LANG=CXX
YACC_LANG=CXX
AM_PROG_LEX
AC_PROG_YACC
# grr stupid autoconf macro doesn't fucking realize that it NEEDS -d to produce
# the appropriate headers, so I added this stupid hack
YACC="$YACC -d"

AC_CHECK_PROGS(BUILD_CC, g++ c++)
AC_ISC_POSIX
AC_HEADER_STDC
#AC_ARG_PROGRAM
AC_C_CONST
AC_C_INLINE
CC=$BUILD_CC
AC_C_BIGENDIAN
AC_TYPE_SIGNAL
AC_TYPE_SIZE_T

AC_CHECK_FUNCS(dlerror)

AC_PATH_PROG(sedpath, sed)
if test "_$sedpath" = _; then
	AC_MSG_ERROR("Cannot find sed: I need it\!")
fi

CXXFLAGS="-Wall -pedantic -rdynamic -ffast-math"
LIBS="$LIBS -ldl"

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
[  --disable-debug	print debug messages to stdout (default on)],
	debug=$enableval, debug=yes)
AC_ARG_ENABLE(,,,)

if test "$debug" = no; then
	CXXFLAGS="$CXXFLAGS -O2"
else
	CXXFLAGS="$CXXFLAGS -g"
fi

if test "$prefix" = NONE; then
   prefix="/usr/local"
fi

if test "$debug" = yes; then
	AC_DEFINE([USE_DEBUG], 1, [Define if you want debugging enabled])
fi

AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [The prefix where thinksynth is installed])

AC_SUBST(CXXFLAGS)
AC_SUBST(LIBS)

#ENV_SOURCES=`find plugins/env/ -iname \"*.c\"`
#FILT_SOURCES=`find plugins/filt/ -iname \"*.c\"`
#MATH_SOURCES=`find plugins/math/ -iname \"*.c\"`
#MISC_SOURCES=`find plugins/misc/ -iname \"*.c\"`
#MIXER_SOURCES=`find plugins/mixer/ -iname \"*.c\"`
#OSC_SOURCES=`find plugins/osc/ -iname \"*.c\"`

#AC_SUBST(ENV_SOURCES)
#AC_SUBST(FILT_SOURCES)
#AC_SUBST(MATH_SOURCES)
#AC_SUBST(MISC_SOURCES)
#AC_SUBST(MIXER_SOURCES)
#AC_SUBST(OSC_SOURCES)

#plugins/env/Makefile
#plugins/filt/Makefile
#plugins/math/Makefile
#plugins/misc/Makefile
#plugins/mixer/Makefile
#plugins/osc/Makefile

AC_OUTPUT([Makefile
src/Makefile
])


echo
echo $PACKAGE $VERSION
echo
echo "compiling for system ....:" $system
echo "debug ...................:" $debug
echo
echo The binary will be installed in $prefix/bin
echo Data will be installed in       $prefix/share/$PACKAGE
echo Now run \'make\' to build $PACKAGE