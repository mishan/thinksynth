dnl $Id: configure.ac,v 1.47 2003/05/16 02:39:10 joshk Exp $

AC_INIT([thinksynth],[0.1])
AC_CONFIG_HEADERS([config.h])

dnl Don't let ancient Autoconfs touch this
AC_PREREQ(2.50)

prefix="/usr/local"

AC_PROG_CXX
AC_PROG_CC

AC_PROG_CXXCPP

AC_LANG(C++)
LEX_LANG=CXX
YACC_LANG=CXX

AC_PROG_YACC

dnl We need bison to build, byacc just doesn't cut it
if test `expr "$YACC" : bison.*` -eq 0; then
	AC_MSG_ERROR([Bison is required to build this program. $YACC just doesn't cut it.])
fi

dnl The autoconf macro doesn't realize that it NEEDS -d to produce
dnl the appropriate headers, so add this stupid hack.

YACC="$YACC -d"
AC_PROG_LEX

if test `expr "$LEX" : .*missing 2>/dev/null` -ne 0; then 
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

AC_PATH_PROG(sedpath, sed)
if test "_$sedpath" = _; then
	AC_MSG_ERROR("Cannot find sed: I need it\!")
fi

CXXFLAGS="-Wall -pedantic -ffast-math"

AC_CHECK_HEADER([sys/soundcard.h],,[AC_MSG_ERROR([You're missing sys/soundcard.h. We need this for sound.])])

AC_CHECK_HEADER([dlfcn.h],,[AC_MSG_ERROR([You're missing dlfcn.h, we need this for dlopen.])])

AC_CHECK_HEADER([libgen.h],[AC_DEFINE(HAVE_LIBGEN_H, 1, [Define if libgen.h is present.])],[LIBS="$LIBS -liberty"])

dnl Linux has libdl
AC_CHECK_LIB(dl, dlopen, [LIBS="$LIBS -ldl"],

dnl Others may have it in libc_r
[AC_CHECK_LIB(c_r, dlopen,
[
	LIBS="$LIBS -lc_r"
	lcr_for_dl=yes
],

dnl Poor cygwin users...
	[AC_CHECK_LIB(cygwin, dlopen,
		[LIBS="$LIBS -lcygwin"],

dnl We need at least one of these
[AC_MSG_ERROR([No dlopen implementation found on your system. Sorry.])])])])

dnl Be smart this time round
if test "$lcr_for_dl" = yes; then
	AC_CHECK_LIB(c_r, dlerror, [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])],)
else
	AC_CHECK_LIB(dl, dlerror, [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])],
		[AC_CHECK_LIB(cygwin, dlerror, [AC_DEFINE(HAVE_DLERROR, 1, [Define if DLERROR is available.])],)])
fi

AC_PATH_PROG(unamepath, uname)
if test "_$unamepath" = _; then
	system="unknown"
else
	AC_MSG_CHECKING(system type)
	system=`$unamepath -s`
	AC_MSG_RESULT($system)
	case "$system" in
		Linux)
			AC_DEFINE([USING_LINUX], 1, [Define if using Linux])
		;;
		FreeBSD)
			AC_DEFINE([USING_FREEBSD], 1, [Define if using FreeBSD])
			CXXFLAGS="$CXXFLAGS -Wno-long-long"
		;;
		Darwin)
		        CXXFLAGS="$CXXFLAGS -traditional-cpp"
		        AC_DEFINE([USING_DARWIN], 1, [Define if using Darwin])
		;;
		CYGWIN*)
			AC_DEFINE([USING_CYGWIN], 1, [Define if using Cygwin/Win32])
			SHARED_SUFFIX=".dll"
			
			dnl XXX this is a crude hack that bloats plugins
			PLUGIN_LDFLAGS="../../src/th*.o -lfl"
		;;
	esac
	
	dnl Cygwin's g++ port doesn't support this, just kill some warnings
	if test `expr $system : CYGWIN` -eq 0; then
		CXXFLAGS="$CXXFLAGS -rdynamic"
		USE_SONAME=yes
		PLUGIN_LDFLAGS="-lm"
		SHARED_SUFFIX=".so"
	fi
fi

AC_SUBST(USE_SONAME)
AC_SUBST(PLUGIN_LDFLAGS)

AC_SUBST(SHARED_SUFFIX)
AC_DEFINE_UNQUOTED([SHARED_SUFFIX], "$SHARED_SUFFIX", [Suffix of shared libraries on this platform.])

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
	CXXFLAGS="$CXXFLAGS -ggdb"
	debug=yes
])

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
	if test "$USE_SONAME" = yes; then
		AC_SUBST(SONAME_FLAGS, ["-Wl,-soname,\$@"])
	fi

	AC_SUBST(USING_GNU_CXX, yes)
	if test "$CXX_MT_BROKEN" = yes; then
		AC_SUBST(DEP_FLAGS, ["-Wp,-MMD,.\$<.dt"])
	else
		AC_SUBST(DEP_FLAGS, ["-Wp,-MMD,.\$<.dt,-MT\$@"])
	fi
fi

AC_SUBST(CXXFLAGS)
AC_SUBST(LIBS)

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

AC_CONFIG_FILES([src/Makefile plugins/plugin-common.mk dsp/Makefile build.mk])
AC_OUTPUT

echo
echo $PACKAGE_NAME $PACKAGE_VERSION
echo
echo "compiling for system ....:" $system
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
