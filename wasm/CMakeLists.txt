# thinksynth for WebAssembly: libthink, the composer host, and every DSP and
# composer plugin the native build makes, compiled with Emscripten to run
# under Node. What it is for is wasm/genwav.mjs -- see the top of that file.
#
#   source ~/emsdk/emsdk_env.sh
#   emcmake cmake -S wasm -B build-wasm
#   cmake --build build-wasm -j
#   node wasm/genwav.mjs -s 60 -o ebb.wav gen/ebb.gen
#
# Its own project rather than an option on the top-level one, because that
# one's first act is to require gtkmm and nothing here has a window. It
# borrows what it can instead: libthink/ and plugins/ are added as they
# stand, so the engine's source list and the plugin list are the native
# build's and cannot drift from it.
#
# What comes out:
#
#   thinksynth.mjs, .wasm   the main module: libthink, the composer host,
#                           sigc++, and the C seam genwav.mjs calls
#   plugins/<category>/*.so the DSP plugins, one side module each
#   plugins/composer/*.so   the composer plugins, likewise
#
# The .so files are wasm, named for the suffix thPluginManager and
# genwav's composer scan look for.

cmake_minimum_required(VERSION 3.20)

project(thinksynth VERSION 0.1 LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
      "Configure this with emcmake; see the top of wasm/CMakeLists.txt.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(THINK_TOP "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Ahead of the tree's own cmake/, so plugins/CMakeLists.txt's
# include(ThinkPlugin) finds the side-module version.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(FetchContent)

# Emscripten's toolchain file says there are no shared libraries, and cmake
# then quietly builds every MODULE as a static archive. There are: a side
# module is one.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

# Everything is position-independent, the main module included, or dynamic
# linking has nothing to relocate. The exception model has to be one model
# across every module, and wasm's own is the one without a JavaScript
# trampoline on every call that might throw.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-fPIC -fwasm-exceptions)
add_link_options(-fwasm-exceptions)

find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

# ---------------------------------------------------------------------------
# config.h, from the tree's own template
# ---------------------------------------------------------------------------

# The plugin path is the build tree's, and the DSP fallback the source
# tree's, since nothing installs any of this.
set(THINK_DEFAULT_OUTPUT "")
set(THINK_PLUGIN_SUFFIX  ".so")
set(THINK_PLUGIN_PATH    "${PROJECT_BINARY_DIR}/plugins/")
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
# the objects go into the main module once, below, and the side modules
# find them there.
add_library(PkgConfig::SIGC INTERFACE IMPORTED GLOBAL)
set_target_properties(PkgConfig::SIGC PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${sigc_SOURCE_DIR};${PROJECT_BINARY_DIR}/sigc")

file(GLOB SIGC_SOURCES
    "${sigc_SOURCE_DIR}/sigc++/*.cc"
    "${sigc_SOURCE_DIR}/sigc++/functors/*.cc")

add_library(sigc OBJECT ${SIGC_SOURCES})
target_compile_definitions(sigc PRIVATE SIGC_BUILD)
target_link_libraries(sigc PUBLIC PkgConfig::SIGC)

# Eight composers draw their state for the Composer window, and include
# cairo.h to do it. The header is the host's -- declarations, nothing
# compiled -- and the calls are left for the loader to bind, which it does
# lazily, on the first call. composer_draw is never called here.
find_path(CAIRO_INCLUDE_DIR cairo.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include
    PATH_SUFFIXES cairo
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED)

add_library(PkgConfig::CAIRO INTERFACE IMPORTED GLOBAL)
set_target_properties(PkgConfig::CAIRO PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CAIRO_INCLUDE_DIR}")

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

add_subdirectory("${THINK_TOP}/libthink" libthink EXCLUDE_FROM_ALL)
add_subdirectory("${THINK_TOP}/plugins" plugins)

# ---------------------------------------------------------------------------
# The main module
# ---------------------------------------------------------------------------

# The composer host is src/CMakeLists.txt's think_composerhost, which cannot
# be borrowed the way the others are: that file builds the application.
add_executable(thinksynth
    "${THINK_TOP}/src/thcPlugin.cpp"
    "${THINK_TOP}/src/thcScheduler.cpp"
    "${THINK_TOP}/src/thcNodeHost.cpp"
    "${THINK_TOP}/src/thcGenFile.cpp"
    "${THINK_TOP}/src/thcGenEdit.cpp"
    glib.cpp
    thinkwasm.cpp)

# shim/ stands in for glibmm; see shim/glibmm.h.
target_include_directories(thinksynth PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${THINK_TOP}"
    "${THINK_TOP}/src")

# Object libraries, not archives: an archive member nothing in the main
# module calls would be left out, and a plugin might be what calls it.
target_link_libraries(thinksynth PRIVATE think_objects sigc)

add_dependencies(thinksynth plugins)

set_target_properties(thinksynth PROPERTIES SUFFIX ".mjs")

# MAIN_MODULE=1 rather than 2: the side modules are compiled apart from this
# and need whatever of libc and libc++ they happen to use, which is not a
# list anyone should have to keep. NODERAWFS makes the file system the
# host's, so dlopen, the .gen, and the dsp/ search all see real paths.
target_link_options(thinksynth PRIVATE
    -sMAIN_MODULE=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createThink
    -sENVIRONMENT=node
    -sNODERAWFS=1
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1MB
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,ENV,HEAPF32,HEAPF64,HEAP32,HEAPU32)
