dnl $Id: configure.ac,v 1.90 2004/09/05 06:53:11 joshk Exp $

AC_INIT([thinksynth],[0.1])
AC_CONFIG_HEADERS([config.h])

dnl Don't let ancient Autoconfs touch this
AC_PREREQ(2.50)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AC_PROG_CXX
AC_PROG_CC

AC_PROG_CXXCPP

dnl Test for a GNU rmdir which supports --ignore-fail-on-non-empty

AC_MSG_CHECKING([if rmdir supports --ignore-fail-on-non-empty])

mkdir .conftest
touch .conftest/x

if rmdir --ignore-fail-on-non-empty .conftest >/dev/null 2>&1; then
  GNU_RMDIR_OPTION="--ignore-fail-on-non-empty"
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

AC_SUBST(GNU_RMDIR_OPTION)

AC_LANG(C++)

LEX_LANG=CXX
YACC_LANG=CXX

AC_PROG_YACC

dnl We need bison to build, byacc just doesn't cut it
if test `expr "$YACC" : bison.*` -eq 0; then
	AC_MSG_ERROR([Bison is required to build this program. $YACC just doesn't cut it.])
fi

AC_PROG_LEX

if test "$LEX" = ":"; then 
	AC_MSG_ERROR([A lex implementation is required to build this program.])
fi

AC_ISC_POSIX
AC_HEADER_STDC
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN

AC_CXX_MT_BROKEN

AC_TYPE_SIGNAL
AC_TYPE_SIZE_T

CXXFLAGS="-Wall -ffast-math"

AC_CHECK_HEADERS(dlfcn.h)

AC_CHECK_HEADER(libgen.h,
[
	AC_DEFINE(HAVE_LIBGEN_H, 1, [Define if libgen.h is present.])
])

AC_CHECK_LIB(iw, iw_get_stats,
[
	AC_CHECK_HEADER([iwlib.h],
	[
		AC_DEFINE(HAVE_IWLIB_H, 1, [Define if iwlib.h is present.])
		LIBS="$LIBS -liw"
	])
])

case "$ac_cv_host" in
	*-freebsd*)
		system=FreeBSD
		AC_DEFINE([USING_FREEBSD], 1, [Define if using FreeBSD])
		CXXFLAGS="$CXXFLAGS -Wno-long-long"
	;;
	*apple-darwin*)
		system="Darwin/OS X"
	        CXXFLAGS="$CXXFLAGS -no-cpp-precomp -fno-common -flat_namespace -undefined suppress"
	        AC_DEFINE([USING_DARWIN], 1, [Define if using Darwin])
		AC_SUBST(NSMODULE_DL_CPP, nsmodule_dl.cpp)
	;;
        *)
		if which uname >/dev/null 2>&1; then
			system=`uname -s`
		else
			system="unknown"
		fi
		if test "$ac_cv_c_compiler_gnu" = yes; then
			CXXFLAGS="$CXXFLAGS -fPIC -DPIC"
		fi
	;;
esac

AC_LINKER_DYNAMIC

rm -f .conftest/x
rmdir .conftest

if test "$system" != "Darwin/OS X"; then
	AC_SEARCH_LIBS(dlopen, [dl c_r], , [AC_MSG_ERROR(No dlopen implementation found on your machine; not continuing.)])
	AC_SEARCH_LIBS(dlerror, [dl c_r], [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])])
	SHARED_SUFFIX=".so"
	PLUGIN_SUFFIX="$SHARED_SUFFIX"
	LIB_LDFLAGS="-shared"
	PLUGIN_LDFLAGS="$LIB_LDFLAGS -lm"
else
	SHARED_SUFFIX=".dylib"
	PLUGIN_SUFFIX=".bundle"
	PLUGIN_LDFLAGS="-bundle -lm"
	LIB_LDFLAGS="-dylib"
fi

AC_SUBST(SHARED_SUFFIX)

AC_SUBST(PLUGIN_SUFFIX)
AC_DEFINE_UNQUOTED([PLUGIN_SUFFIX], "$PLUGIN_SUFFIX", [Suffix of shared libraries on this platform.])

AC_SUBST(PLUGIN_LDFLAGS)
AC_SUBST(LIB_LDFLAGS)

AC_ARG_ENABLE(debug,
AC_HELP_STRING([--enable-debug],[print debug messages to stdout (default on)]),
[debug="$enableval"], [debug=no])

if test "$debug" = no; then
	CXXFLAGS="$CXXFLAGS -O2"
else
	AC_DEFINE([USE_DEBUG], 1, [Define if you want debugging enabled])
	CXXFLAGS="$CXXFLAGS -g3 -W -Wno-unused-parameter"
fi

AC_ARG_ENABLE(efence,
	[AC_HELP_STRING([--enable-efence],[Enable ElectricFence in the build])],
	[AC_CHECK_LIB(efence, malloc,
		dnl we found efence
		[
			LIBS="$LIBS -lefence"
			efence=yes
		],
		dnl no efence available :/
		[
			AC_MSG_WARN([--enable-efence specified but could not link to it - disabling])
			efence=no
		])
	])
	
AC_ARG_ENABLE(,,,)

AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [The prefix where thinksynth is installed])

dnl Construction of plugin-common.inc

if test "$ac_cv_cxx_compiler_gnu" = yes; then
	AC_SUBST(USING_GNU_CXX, yes)
	if test "$CXX_MT_BROKEN" = yes; then
		AC_SUBST(DEP_FLAGS, ["-Wp,-MMD,.\$<.dt"])
	else
		AC_SUBST(DEP_FLAGS, ["-Wp,-MMD,.\$<.dt,-MT\$@"])
	fi
fi

AC_CHECK_FUNC(basename,,
[
  BASENAME_CPP=basename.cpp
  AC_DEFINE(USE_EXTERNAL_BASENAME, 1, [Define if a usable basename(3) is available.])
])

AC_SUBST(BASENAME_CPP)

AC_MSG_CHECKING([for gthread-2.0])

if ! pkg-config gthread-2.0 --libs >/dev/null 2>&1; then
  AC_MSG_RESULT([not found])
  AC_MSG_ERROR([You should install the Glib development headers before building this software. Try libglib2.0-dev in Debian.])
else
  AC_MSG_RESULT([found])
  AC_SUBST(GTHREAD_LIBS, [`pkg-config gthread-2.0 --libs`])
fi

if test "$system" != "Darwin/OS X"; then

AC_MSG_CHECKING([for GTK--]) 

if ! pkg-config gtkmm-2.0 --cflags >/dev/null 2>&1; then
  AC_MSG_RESULT([not found])
  AC_MSG_ERROR([You should install the GTK-- headers before building this software. Try libgtkmm2.0-dev in Debian.])
else
  AC_MSG_RESULT([found])
  AC_SUBST(GTKMM_CFLAGS, [`pkg-config gtkmm-2.0 --cflags`])
  AC_SUBST(GTKMM_LIBS,  [`pkg-config gtkmm-2.0 --libs`])
fi

AC_MSG_CHECKING([for JACK])

if ! pkg-config --cflags jack >/dev/null 2>&1; then
  AC_MSG_RESULT([not found])
  have_jack=0
else
  AC_MSG_RESULT([found])
  AC_SUBST(JACK_CFLAGS, [`pkg-config --cflags jack`])
  AC_SUBST(JACK_LIBS, [`pkg-config --libs jack`])
  have_jack=1
fi

AC_DEFINE_UNQUOTED(HAVE_JACK, $have_jack, [Define to 1 if JACK is available.])

AC_MSG_CHECKING([for ALSA])

if ! pkg-config --cflags alsa >/dev/null 2>&1; then
  AC_MSG_RESULT([not found])
  have_alsa=0
else
  AC_MSG_RESULT([found])
  AC_SUBST(ALSA_CFLAGS, [`pkg-config --cflags alsa`])
  AC_SUBST(ALSA_LIBS, [`pkg-config --libs alsa`])
  have_alsa=1
fi

AC_DEFINE_UNQUOTED(HAVE_ALSA, $have_alsa, [Define to 1 if ALSA files are available.])

else

have_alsa=0
have_jack=0

fi

AC_MSG_CHECKING([for SigC++])

if pkg-config --cflags sigc++-1.2 >/dev/null 2>&1; then
	SIGC_CFLAGS=`pkg-config --cflags sigc++-1.2`
	SIGC_LIBS=`pkg-config --libs sigc++-1.2`
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
	AC_MSG_ERROR([You need SigC++ to build libthink. Try libsigc++1.2-dev in Debian.])
fi

AC_SUBST(SIGC_CFLAGS)
AC_SUBST(SIGC_LIBS)
AC_SUBST(CXXFLAGS)
AC_SUBST(LIBS)
AC_SUBST(ALSA_LIBS)

dnl Flatten out all of the paths.
AC_SUBST_DIR(libdir)
AC_SUBST_DIR(datadir)
AC_SUBST_DIR(bindir)

AC_ARG_WITH(plugin-path,
	[AC_HELP_STRING([--with-plugin-path], [Define a path to install plugins into.])],
	[plugin_path=$withval],
	[plugin_path=${libdir}/${PACKAGE_NAME}/plugins/])

dnl Someone may have been a smartass and configure with
dnl ./configure --with-plugin-path=${prefix}/bar, so flatten that too

AC_SUBST_DIR(plugin_path)

dnl sources need this
if test `expr $plugin_path : .*/$` -eq 0; then
	plugin_path=$plugin_path/
fi

AC_DEFINE_UNQUOTED([PLUGIN_PATH], "$plugin_path", [Path to plugins.])
AC_SUBST(plugin_path)

AC_CONFIG_FILES([libthink/Makefile src/Makefile src/gui/Makefile plugins/plugin-common.mk plugins/Makefile dsp/Makefile build.mk])
AC_OUTPUT

echo
echo $PACKAGE_NAME $PACKAGE_VERSION
echo
echo "compiling for system ....: $system"
echo -n "ALSA support ............: "

if test "$have_alsa" -eq 1; then
  echo enabled
else
  echo disabled
fi
echo -n "JACK support ............: "
if test "$have_jack" -eq 1; then
  echo enabled
else
  echo disabled
fi

echo -n "debug ...................: $debug "

if test "$efence" = yes; then
	echo "(ElectricFence enabled)"
else
	echo
fi

echo
echo The binary will be installed in $bindir
echo DSPs will be installed in       $datadir/$PACKAGE_NAME
echo Plugins will be installed in    $plugin_path
echo
echo "Now run 'make' to build $PACKAGE_NAME"

# vim:noet:ai:ts=8
