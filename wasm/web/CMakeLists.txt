# thinksynth for the browser: libthink and every DSP plugin, linked into one
# wasm module for an AudioWorklet, and the page that plays it. This is M1 of
# JAM.md -- a synth in a tab.
#
#   source ~/emsdk/emsdk_env.sh
#   emcmake cmake -S wasm/web -B build-web
#   cmake --build build-web -j
#   node wasm/web/serve.mjs            and open http://localhost:8080/
#
# The Node build in wasm/ loads plugins as side modules through dlopen. A
# worklet has no loader and no files, so here every plugin is compiled in
# (cmake/ThinkPlugin.cmake) and found through a table instead
# (THINK_STATIC_PLUGINS, libthink/thDynLib.h). Everything else -- libthink's
# sources, the plugin list, sigc++, config.h -- is wasm/'s, from
# ../cmake/ThinkEmscripten.cmake.
#
# What comes out, in the build directory, is the whole site: index.html,
# main.js, worklet.js, thinkweb.mjs and .wasm, and the shipped .dsp files
# the page offers, with dsp/index.json listing them.

cmake_minimum_required(VERSION 3.20)

project(thinksynth VERSION 0.1 LANGUAGES C CXX)

get_filename_component(THINK_TOP "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# This directory's ThinkPlugin.cmake ahead of wasm/'s, and wasm/cmake/ for
# what the two builds share.
list(PREPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

include(ThinkEmscripten)

# Plugins come out of a table rather than off the disk. PUBLIC, because the
# table's type is declared behind the same macro and the generated table
# below has to see it.
target_compile_definitions(think_objects PUBLIC THINK_STATIC_PLUGINS)

# ---------------------------------------------------------------------------
# The table
# ---------------------------------------------------------------------------

# One row per plugin cmake/ThinkPlugin.cmake was handed, naming the three
# functions its namespace defines. A plugin whose functions do not match
# these declarations fails at the link, which is the right place.
get_property(STATIC_PLUGINS GLOBAL PROPERTY THINK_STATIC_PLUGINS)
get_property(STATIC_SOURCES GLOBAL PROPERTY THINK_STATIC_SOURCES)

set(decls "")
set(rows "")

foreach(p IN LISTS STATIC_PLUGINS)
  string(REPLACE "/" "_" id "${p}")
  string(APPEND decls
"namespace thp_${id} {
    int  module_init (thPlugin *);
    int  module_callback (thNode *, thSynthTree *, unsigned int, unsigned int);
    void module_cleanup (thPlugin *);
}
")
  string(APPEND rows
"    { \"${p}\", (void *)&thp_${id}::module_init,
      (void *)&thp_${id}::module_callback, (void *)&thp_${id}::module_cleanup },
")
endforeach()

list(LENGTH STATIC_PLUGINS STATIC_COUNT)

file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/static/registry.cpp" CONTENT
"/* The table thDynLib's static branch reads: every DSP plugin in
   plugins/CMakeLists.txt. Written by wasm/web/CMakeLists.txt. */
#include \"thinkstatic.h\"
#include \"thDynLib.h\"

${decls}
const thStaticPlugin thStaticPlugins[] = {
${rows}};

const size_t thStaticPluginCount = ${STATIC_COUNT};
")

# ---------------------------------------------------------------------------
# The module
# ---------------------------------------------------------------------------

add_executable(thinkweb
    ${STATIC_SOURCES}
    "${PROJECT_BINARY_DIR}/static/registry.cpp"
    "${THINK_TOP}/src/gthSynthSource.cpp"
    thinkweb.cpp)

target_include_directories(thinkweb PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THINK_TOP}"
    "${THINK_TOP}/src")

target_link_libraries(thinkweb PRIVATE think_objects sigc)

set_target_properties(thinkweb PROPERTIES SUFFIX ".mjs")

# web for the worklet, node for the tests that drive the same module
# directly. The worklet is handed the .wasm's bytes and instantiates the
# module from them (instantiateWasm, in worklet.js; host.js says why bytes
# and not a compiled module), so nothing here fetches.
target_link_options(thinkweb PRIVATE
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createThinkWeb
    -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1MB
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,HEAPF32)

# ---------------------------------------------------------------------------
# The site
# ---------------------------------------------------------------------------

# The page and its scripts, copied beside the module so the build directory
# is a site on its own: serve it, or put it anywhere that serves files.
set(SITE_SOURCES index.html main.js host.js worklet.js)
set(SITE_OUTPUTS "")

foreach(f IN LISTS SITE_SOURCES)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/${f}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${PROJECT_BINARY_DIR}/${f}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/${f}")
endforeach()

# The shipped patches -- dsp/*.dsp, not the noargs/ and old/ drawers -- and
# the list the page's menu is filled from.
file(GLOB SHIPPED_DSP "${THINK_TOP}/dsp/*.dsp")
list(SORT SHIPPED_DSP)

set(dsp_names "")

foreach(f IN LISTS SHIPPED_DSP)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/dsp/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${f}" "${PROJECT_BINARY_DIR}/dsp/${name}"
      DEPENDS "${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/dsp/${name}")
  list(APPEND dsp_names "\"${name}\"")
endforeach()

list(JOIN dsp_names ", " dsp_names)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/dsp/index.json"
     CONTENT "[${dsp_names}]\n")

add_custom_target(site ALL DEPENDS ${SITE_OUTPUTS})
