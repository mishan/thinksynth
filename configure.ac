dnl $Id$

AC_INIT([thinksynth],[0.1])
AC_CONFIG_HEADERS([config.h])

dnl Don't let ancient Autoconfs touch this
AC_PREREQ(2.50)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AC_PROG_CXX
AC_PROG_CC

AC_PROG_CXXCPP

dnl Update this when:
dnl 	- Functions or symbols in the library have been /removed/ or have 
dnl	  had their type changed
dnl	- Function /arguments/ have changed
dnl	- Function /behaviors/ have changed such that users need to make
dnl	  revisions to the calls to have them work the way they did before
dnl	  (bad)
lib_major=3

dnl Update this when:
dnl	- The new library needs new supporting libraries, but nothing else
dnl	  in the API has changed. For example, old GUI depends on old SigC,
dnl	  and will run with a new libthink, but major, non-deterministic
dnl	  breakage will occur. Or vice-versa.
dnl	- The library was misbuilt on all supported architectures and
dnl	  was released that way.
lib_minor=1
lib_revision="${lib_major}.${lib_minor}"

AC_SUBST(lib_revision)

dnl Test for a GNU rmdir which supports --ignore-fail-on-non-empty

AC_MSG_CHECKING([if rmdir supports --ignore-fail-on-non-empty])

rm -rf .conftest
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

AC_CHECK_LIB(cposix, strerror, [LIBS="$LIBS -lcposix"])
AC_HEADER_STDC
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN

AC_CXX_MT_BROKEN

AC_TYPE_SIGNAL
AC_TYPE_SIZE_T

CXXFLAGS="-Wall -ffast-math"

AC_CHECK_HEADERS([dlfcn.h libgen.h])

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
		if test "$ac_cv_c_compiler_gnu" = yes; then
			CXXFLAGS="$CXXFLAGS -fPIC -DPIC"
		fi
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

PKG_CHECK_MODULES(GTHREAD, gthread-2.0)

if test "$system" != "Darwin/OS X"; then

PKG_CHECK_MODULES(GTKMM, gtkmm-2.4)

PKG_CHECK_MODULES(JACK, jack,
[
	have_jack=1
	AC_DEFINE(HAVE_JACK, 1, [Define to 1 if JACK is available.])
	GTH_JACK_AUDIO=gthJackAudio.o
	
], have_jack=0)

PKG_CHECK_MODULES(ALSA, alsa, 
[
	have_alsa=1
	AC_DEFINE(HAVE_ALSA, 1, [Define if ALSA is available.])
	GTH_ALSA="gthALSAAudio.o gthALSAMidi.o"
], have_alsa=0)

else

have_alsa=0
have_jack=0

fi

AC_SUBST(GTH_ALSA)
AC_SUBST(GTH_JACK_AUDIO)

PKG_CHECK_MODULES(SIGC, sigc++-2.0)

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

AC_ARG_WITH(dsp-path,
	[AC_HELP_STRING([--with-dsp-path], [Define a path to install DSPs into.])],
	[dsp_path=$withval],
	[dsp_path=${datadir}/${PACKAGE_NAME}/dsp/])

AC_ARG_WITH(patch-path,
	[AC_HELP_STRING([--with-patch-path], [Define a path to install patches into.])],
	[patch_path=$withval],
	[patch_path=${datadir}/${PACKAGE_NAME}/patches/])
	
dnl Someone may have been a smartass and configured with
dnl ./configure --with-dsp-path=${prefix}/bar, so flatten that too

AC_SUBST_DIR(dsp_path)
AC_SUBST_DIR(patch_path)

AC_ARG_WITH(default-output,
[AC_HELP_STRING([--with-default-output], [Choose either 'jack', 'alsa', or 'none' (default jack)])],
[
	if test "$withval" = alsa && test "$have_alsa" -eq 0; then
		if test "$have_jack" -eq 1; then
			default_output="jack"
		else
			default_output="none"
		fi
		AC_MSG_WARN(['$withval' output not available, using '$default_output'])
	elif test "$withval" = jack && test "$have_jack" -eq 0; then
		if test "$have_alsa" -eq 1; then
			default_output="alsa"
		else
			default_output="none"
		fi
		AC_MSG_WARN(['$withval' output not available, using '$default_output'])
	else
		case "$withval" in
			alsa | none | jack) default_output="$withval" ;;
			*)
				if test "$have_jack" -eq 1; then
					default_output="jack"
				elif test "$have_alsa" -eq 1; then
					default_output="alsa"
				else
					default_output="none"
				fi
				AC_MSG_WARN([unrecognized output '$withval', defaulting to '$default_output'])
			;;
		esac
	fi
],
[
	if test "$have_jack" -eq 1; then
		default_output="jack"
	elif test "$have_alsa" -eq 1; then
		default_output="alsa"
	else
		default_output="none"
	fi
])

AC_DEFINE_UNQUOTED([DEFAULT_OUTPUT], "$default_output", [Default output device for thinksynth.])
AC_SUBST(DEFAULT_OUTPUT, "$default_output")

thinkrc=$sysconfdir/thinkrc
AC_SUBST_DIR(thinkrc)

AC_DEFINE_UNQUOTED([DEFAULT_THINKRC], "$thinkrc", [Path to default configuration file.])

dnl sources need this
if test `expr $dsp_path : .*/$` -eq 0; then
	dsp_path=$dsp_path/
fi

if test `expr $patch_path : .*/$` -eq 0; then
	patch_path=$patch_path/
fi

AC_DEFINE_UNQUOTED([DSP_PATH], "$dsp_path", [Path to DSPs.])
AC_DEFINE_UNQUOTED([PATCH_PATH], "$patch_path",[Path to patches.])
AC_DEFINE_UNQUOTED([PLUGIN_PATH], "$plugin_path", [Path to plugins.])
AC_SUBST(dsp_path)
AC_SUBST(patch_path)
AC_SUBST(plugin_path)

AC_CONFIG_FILES([
	libthink/Makefile
	src/Makefile
	src/gui/Makefile
	plugins/Makefile
	patches/Makefile
	dsp/Makefile
	etc/Makefile
	docs/Makefile
	etc/thinkrc
	docs/thinksynth.1
	plugins/plugin-common.mk
	build.mk
])

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

echo    "default output device ...: $default_output"

echo -n "debug ...................: $debug "

if test "$efence" = yes; then
	echo "(ElectricFence enabled)"
else
	echo
fi

echo
echo The binary will be installed in $bindir
echo DSPs will be installed in       $dsp_path
echo Patches will be installed in    $patch_path
echo Plugins will be installed in    $plugin_path
echo
echo "Now run 'make' to build $PACKAGE_NAME"

# vim:noet:ai:ts=4
