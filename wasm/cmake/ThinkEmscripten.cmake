# What both Emscripten builds of thinksynth share: the Node build in wasm/
# (plugins as side modules, for genwav.mjs) and the browser build in
# wasm/web/ (plugins compiled in, for an AudioWorklet).
#
# Included after project(), with THINK_TOP set to the source tree and
# CMAKE_MODULE_PATH already pointing at the including build's own
# ThinkPlugin.cmake. That file is the one thing the two builds do not
# share, and plugins/CMakeLists.txt, added at the bottom of this one, is
# what reads it. Anything that must reach every target -- -fPIC, for the
# side-module build -- has to be said before the include.
#
# THINK_PLUGIN_PATH may be set first; it goes into config.h as the plugin
# root. THINK_NEED_CAIRO says the including build compiles the composers;
# see the cairo section below.

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
      "Configure this with emcmake; see the top of wasm/CMakeLists.txt.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(GNUInstallDirs)
include(FetchContent)

# One exception model across everything linked together, and wasm's own is
# the one without a JavaScript trampoline on every call that might throw.
add_compile_options(-fwasm-exceptions)
add_link_options(-fwasm-exceptions)

find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

# ---------------------------------------------------------------------------
# config.h, from the tree's own template
# ---------------------------------------------------------------------------

# The DSP fallback is the source tree's, since nothing installs any of this.
if(NOT DEFINED THINK_PLUGIN_PATH)
  set(THINK_PLUGIN_PATH "")
endif()

set(THINK_DEFAULT_OUTPUT "")
set(THINK_PLUGIN_SUFFIX  ".so")
set(THINK_DSP_PATH       "${THINK_TOP}/dsp/")
set(THINK_PATCH_PATH     "${THINK_TOP}/patches/")
set(THINK_APP_ID         "org.thinksynth.thinksynth")
set(HAVE_DLFCN_H 1)
set(HAVE_DLERROR 1)

configure_file("${THINK_TOP}/cmake/config.h.in" "${PROJECT_BINARY_DIR}/config.h")

include_directories("${PROJECT_BINARY_DIR}")

# ---------------------------------------------------------------------------
# sigc++
# ---------------------------------------------------------------------------

# thArg carries a sigc::signal and the scheduler hands out sigc::connections,
# so sigc++ is compiled in -- from source, the version the native build
# links, and without its own build system, which would want to build its
# tests and examples too. SOURCE_SUBDIR names a directory that does not
# exist so MakeAvailable fetches and stops.
FetchContent_Declare(sigc
    URL https://github.com/libsigcplusplus/libsigcplusplus/archive/refs/tags/3.6.0.tar.gz
    URL_HASH SHA256=bbe81e4f6d8acb41a9795525a38c0782751dbc4af3d78a9339f4a282e8a16c38
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(sigc)

file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/sigc/sigc++config.h" CONTENT [=[
#ifndef SIGCXXCONFIG_H_INCLUDED
#define SIGCXXCONFIG_H_INCLUDED
#define SIGCXX_MAJOR_VERSION 3
#define SIGCXX_MINOR_VERSION 6
#define SIGCXX_MICRO_VERSION 0
#define SIGC_CONFIGURE 1
#define SIGC_API
#endif
]=])

# Under the name the tree's CMakeLists files link against. Headers only:
# the objects are linked into the main module once, as sigc below, and
# anything else finds them there.
add_library(PkgConfig::SIGC INTERFACE IMPORTED GLOBAL)
set_target_properties(PkgConfig::SIGC PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${sigc_SOURCE_DIR};${PROJECT_BINARY_DIR}/sigc")

file(GLOB SIGC_SOURCES
    "${sigc_SOURCE_DIR}/sigc++/*.cc"
    "${sigc_SOURCE_DIR}/sigc++/functors/*.cc")

# An object library, not an archive: an archive member nothing in the main
# module calls would be left out, and a side module might be what calls it.
add_library(sigc OBJECT ${SIGC_SOURCES})
target_compile_definitions(sigc PRIVATE SIGC_BUILD)
target_link_libraries(sigc PUBLIC PkgConfig::SIGC)

# Eight composers draw their state for the Composer window, and include
# cairo.h to do it. The header is the host's -- declarations, nothing
# compiled -- and the calls are left for the loader to bind, which it does
# lazily, on the first call. composer_draw is never called here.
#
# Only the Node build wants the host's header, and it says so with
# THINK_NEED_CAIRO. The browser build never looks: it compiles the draws
# against wasm/cairo2d instead, which records them for the page to replay
# (JAM_M6.md, section 3), so it builds where the host has no cairo headers
# at all. plugins/CMakeLists.txt names the target in both, so the target is
# always there -- empty when nothing looked.
add_library(PkgConfig::CAIRO INTERFACE IMPORTED GLOBAL)

if(THINK_NEED_CAIRO)
  find_path(CAIRO_INCLUDE_DIR cairo.h
      PATHS /usr/include /usr/local/include /opt/homebrew/include
      PATH_SUFFIXES cairo
      NO_CMAKE_FIND_ROOT_PATH
      REQUIRED)

  set_target_properties(PkgConfig::CAIRO PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CAIRO_INCLUDE_DIR}")
endif()

# ---------------------------------------------------------------------------
# libthink and the plugins, as the tree has them
# ---------------------------------------------------------------------------

# What libthink/CMakeLists.txt expects the top level to have said. Only its
# think_objects is used; the shared library it would also make is never
# built -- the main module is where libthink lives here.
set(THINK_LIB_MAJOR 6)
set(THINK_LIB_MINOR 0)
set(THINK_BUILD_STATIC_LIB OFF)
set(THINK_PKG_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")

# The composer host: src/CMakeLists.txt's think_composerhost, which cannot
# be borrowed the way libthink/ and plugins/ are, since that file builds the
# application. Named here once for the two builds that compile it
# themselves -- wasm/ for Node and wasm/web/ for the worklet -- so the two
# wasm hosts M2's gate compares cannot be built from different source sets.
# glib.cpp and shim/ stand in for glibmm; see shim/glibmm.h.
set(THINK_COMPOSERHOST_SOURCES
    "${THINK_TOP}/src/thcPlugin.cpp"
    "${THINK_TOP}/src/thcScheduler.cpp"
    "${THINK_TOP}/src/thcNodeHost.cpp"
    "${THINK_TOP}/src/thcGenFile.cpp"
    "${THINK_TOP}/src/thcGenEdit.cpp"
    "${THINK_TOP}/wasm/glib.cpp")

add_subdirectory("${THINK_TOP}/libthink" libthink EXCLUDE_FROM_ALL)
add_subdirectory("${THINK_TOP}/plugins" plugins)
