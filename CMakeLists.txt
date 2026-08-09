# thinksynth
#
# Replaces autotools + the hand-rolled build.mk. See PORTING.md section 7.
#
# This is deliberately a faithful reproduction of what configure.ac and
# build.mk did, not an improvement on it: the point of step 1 is that the
# CMake build and the autotools build produce the same artefacts and the same
# dspcheck/dsplevel results, so that anything that breaks later is
# attributable to one change rather than two. The cleanups PORTING.md section
# 6 calls for -- dropping -ffast-math, linking plugins against libthink,
# C++17, visibility -- are deliberately NOT done here. Each is flagged below
# with the section that will do it.

cmake_minimum_required(VERSION 3.16)

project(thinksynth
        VERSION 0.1
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakeDependentOption)
include(CheckIncludeFileCXX)
include(CheckCXXSourceCompiles)
include(TestBigEndian)

# ---------------------------------------------------------------------------
# Library version
#
# Straight from configure.ac. Bump lib_major when libthink's ABI changes; the
# comment block in configure.ac records why it is at 6.
# ---------------------------------------------------------------------------

set(THINK_LIB_MAJOR 6)
set(THINK_LIB_MINOR 0)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(THINK_ENABLE_DEBUG "Print debug messages to stdout" OFF)
option(THINK_BUILD_HARNESSES "Build the scripts/ test harnesses" ON)
option(THINK_BUILD_STATIC_LIB "Also build libthink as a static archive" ON)

# configure.ac:108 put -ffast-math on every object *and* the link line, which
# sets FTZ/DAZ process-wide including for the GUI. PORTING.md section 6 wants
# it gone; it stays ON here only so that this build is comparable to the
# autotools one. Flip the default in the cleanup pass and re-run dsplevel.
option(THINK_FAST_MATH "Build with -ffast-math (as autotools did)" ON)

# Sanitizers are applied globally rather than per-target on purpose. An
# earlier round of measurements was worthless because the flags reached only
# the harness and not libthink or the plugins -- see REVIVAL.md, "The queue
# rework, and a bad measurement".
set(THINK_SANITIZE "" CACHE STRING
    "Sanitizers to build everything with, e.g. address,undefined or thread")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  # autotools defaulted to -O2 with no -g; RelWithDebInfo is -O2 plus symbols.
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# libthink is found next to the binary in the build tree and via the install
# prefix afterwards, which is what src/Makefile.in's $ORIGIN rpath did.
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)

add_compile_options(-Wall)

if(THINK_FAST_MATH)
  add_compile_options(-ffast-math)
endif()

if(THINK_ENABLE_DEBUG)
  add_compile_options(-W -Wno-unused-parameter)
endif()

if(THINK_SANITIZE)
  add_compile_options(-fsanitize=${THINK_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${THINK_SANITIZE})
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SIGC   REQUIRED IMPORTED_TARGET sigc++-2.0)
pkg_check_modules(GTKMM  REQUIRED IMPORTED_TARGET gtkmm-3.0)
pkg_check_modules(GTHREAD REQUIRED IMPORTED_TARGET gthread-2.0)

pkg_check_modules(JACK IMPORTED_TARGET jack)
pkg_check_modules(ALSA IMPORTED_TARGET alsa)

if(JACK_FOUND)
  set(HAVE_JACK 1)
endif()

# configure.ac skipped the ALSA probe entirely on Darwin.
if(ALSA_FOUND AND NOT APPLE)
  set(HAVE_ALSA 1)
endif()

if(HAVE_JACK)
  set(THINK_DEFAULT_OUTPUT jack)
elseif(HAVE_ALSA)
  set(THINK_DEFAULT_OUTPUT alsa)
else()
  set(THINK_DEFAULT_OUTPUT none)
endif()

# ---------------------------------------------------------------------------
# Platform probes
# ---------------------------------------------------------------------------

check_include_file_cxx(dlfcn.h HAVE_DLFCN_H)

if(NOT APPLE)
  # AC_SEARCH_LIBS(dlopen, [dl c_r]) -- glibc 2.34+ folded libdl into libc, so
  # find_library may legitimately come up empty. Only fail if dlopen is
  # genuinely unavailable.
  find_library(THINK_DL_LIBRARY dl)
  if(NOT THINK_DL_LIBRARY)
    set(THINK_DL_LIBRARY "")
  endif()
endif()

set(CMAKE_REQUIRED_LIBRARIES ${THINK_DL_LIBRARY})
check_cxx_source_compiles("
  #include <dlfcn.h>
  int main() { dlerror(); return 0; }
" HAVE_DLERROR)
unset(CMAKE_REQUIRED_LIBRARIES)

check_cxx_source_compiles("
  #include <signal.h>
  int main() { sighandler_t h = 0; (void)h; return 0; }
" THINK_HAVE_SIGHANDLER_T)

if(NOT THINK_HAVE_SIGHANDLER_T)
  check_cxx_source_compiles("
    #include <signal.h>
    int main() { sig_t h = 0; (void)h; return 0; }
  " THINK_HAVE_SIG_T)
  if(THINK_HAVE_SIG_T)
    set(USE_SIG_T 1)
  else()
    set(GUESS_SIG_T 1)
  endif()
endif()

test_big_endian(THINK_BIG_ENDIAN)
if(THINK_BIG_ENDIAN)
  set(WORDS_BIGENDIAN 1)
endif()

if(APPLE)
  set(USING_DARWIN 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(USING_FREEBSD 1)
endif()

if(THINK_ENABLE_DEBUG)
  set(USE_DEBUG 1)
endif()

# ---------------------------------------------------------------------------
# Install layout, and the paths baked into config.h
#
# The trailing slashes matter: the sources concatenate a bare filename onto
# each of these.
# ---------------------------------------------------------------------------

if(WIN32)
  set(THINK_PLUGIN_SUFFIX ".dll")
elseif(APPLE)
  set(THINK_PLUGIN_SUFFIX ".dylib")
else()
  set(THINK_PLUGIN_SUFFIX ".so")
endif()

set(THINK_PLUGIN_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME}/plugins/")
set(THINK_DSP_PATH    "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/dsp/")
set(THINK_PATCH_PATH  "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/patches/")
set(THINK_DEFAULT_THINKRC "${CMAKE_INSTALL_FULL_SYSCONFDIR}/thinkrc")

configure_file(cmake/config.h.in "${PROJECT_BINARY_DIR}/config.h")

# Every translation unit does #include "config.h", unqualified.
include_directories("${PROJECT_BINARY_DIR}")

# ---------------------------------------------------------------------------

enable_testing()

add_subdirectory(libthink)
add_subdirectory(plugins)
add_subdirectory(src)

if(THINK_BUILD_HARNESSES)
  add_subdirectory(scripts)
endif()

install(DIRECTORY dsp/     DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/dsp"
        FILES_MATCHING PATTERN "*.dsp")
install(DIRECTORY patches/ DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/patches"
        FILES_MATCHING PATTERN "*.patch")

# ---------------------------------------------------------------------------
# The two generated data files that used to come from etc/Makefile.in and
# docs/Makefile.in.
#
# Their templates still spell substitutions the way configure did -- @dsp_path@
# rather than @THINK_DSP_PATH@ -- so the variables are aliased here instead of
# rewriting files whose contents are not really ours to reformat.
# ---------------------------------------------------------------------------

set(dsp_path       "${THINK_DSP_PATH}")
set(patch_path     "${THINK_PATCH_PATH}")
set(plugin_path    "${THINK_PLUGIN_PATH}")
set(thinkrc        "${THINK_DEFAULT_THINKRC}")
set(DEFAULT_OUTPUT "${THINK_DEFAULT_OUTPUT}")
set(PACKAGE_VERSION "${PROJECT_VERSION}")

configure_file(etc/thinkrc.in        "${PROJECT_BINARY_DIR}/etc/thinkrc" @ONLY)
configure_file(docs/thinksynth.1.in  "${PROJECT_BINARY_DIR}/docs/thinksynth.1" @ONLY)

install(FILES "${PROJECT_BINARY_DIR}/etc/thinkrc"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}")

install(FILES "${PROJECT_BINARY_DIR}/docs/thinksynth.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")

# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  system ................: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  build type ............: ${CMAKE_BUILD_TYPE}")
message(STATUS "  ALSA ..................: ${HAVE_ALSA}")
message(STATUS "  JACK ..................: ${HAVE_JACK}")
message(STATUS "  default output ........: ${THINK_DEFAULT_OUTPUT}")
message(STATUS "  debug .................: ${THINK_ENABLE_DEBUG}")
message(STATUS "  -ffast-math ...........: ${THINK_FAST_MATH}")
message(STATUS "  sanitizers ............: ${THINK_SANITIZE}")
message(STATUS "  libthink soversion ....: ${THINK_LIB_MAJOR}")
message(STATUS "  plugins install to ....: ${THINK_PLUGIN_PATH}")
message(STATUS "")
