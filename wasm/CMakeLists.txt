# thinksynth for WebAssembly under Node: libthink, the composer host, and
# every DSP and composer plugin the native build makes, compiled with
# Emscripten. What it is for is wasm/genwav.mjs -- see the top of that file.
#
#   source ~/emsdk/emsdk_env.sh
#   emcmake cmake -S wasm -B build-wasm
#   cmake --build build-wasm -j
#   node wasm/genwav.mjs -s 60 -o ebb.wav gen/ebb.gen
#
# Its own project rather than an option on the top-level one, because that
# one's first act is to require gtkmm and nothing here has a window. It
# borrows what it can instead: libthink/ and plugins/ are added as they
# stand (cmake/ThinkEmscripten.cmake, which wasm/web/ shares), so the
# engine's source list and the plugin list are the native build's and
# cannot drift from it.
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

get_filename_component(THINK_TOP "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Ahead of the tree's own cmake/, so plugins/CMakeLists.txt's
# include(ThinkPlugin) finds the side-module version.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Emscripten's toolchain file says there are no shared libraries, and cmake
# then quietly builds every MODULE as a static archive. There are: a side
# module is one.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

# Everything is position-independent, the main module included, or dynamic
# linking has nothing to relocate. Said before the shared part is included,
# so it reaches libthink and the plugins too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-fPIC)

# The build tree's plugins, which is where genwav.mjs's -p looks by default.
set(THINK_PLUGIN_PATH "${PROJECT_BINARY_DIR}/plugins/")

include(ThinkEmscripten)

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
