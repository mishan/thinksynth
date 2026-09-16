# thinksynth for the browser: libthink, every DSP plugin and every composer,
# with the composer host, linked into one wasm module for an AudioWorklet,
# and the page that plays it. M1 of JAM.md was the synth in a tab; M2 is the
# piece in a tab, which is the scheduler running in the worklet too.
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
# main.js, worklet.js, thinkweb.mjs and .wasm, and the shipped .dsp and .gen
# files the page offers, each directory with an index.json listing it.

cmake_minimum_required(VERSION 3.20)

project(thinksynth VERSION 0.1 LANGUAGES C CXX)

get_filename_component(THINK_TOP "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# This directory's ThinkPlugin.cmake ahead of wasm/'s, and wasm/cmake/ for
# what the two builds share.
list(PREPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

# What a composer may export, and the prototype the generated table declares
# each by -- `@' is where the name goes. Said here rather than in
# ThinkPlugin.cmake because the table is written here and the list has to
# reach both; plugins/ is a subdirectory and inherits it.
#
# composer_draw is deliberately absent; ThinkPlugin.cmake says why.
set(THINK_COMPOSER_EXPORTS init create tick receive param_changed input
                           capture destroy)

set(THINK_COMPOSER_PROTO_init          "int @ (thcComposerInfo *)")
set(THINK_COMPOSER_PROTO_create        "void *@ (const thcParams *)")
set(THINK_COMPOSER_PROTO_tick
    "double @ (void *, const thcTransport *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_receive
    "void @ (void *, const thcEvent *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_param_changed "void @ (void *, int)")
set(THINK_COMPOSER_PROTO_input         "void @ (void *, const thcInputEvent *)")
set(THINK_COMPOSER_PROTO_capture       "const char *@ (void *, int)")
set(THINK_COMPOSER_PROTO_destroy       "void @ (void *)")

include(ThinkEmscripten)

# Plugins come out of a table rather than off the disk. PUBLIC, because the
# table's type is declared behind the same macro and the generated table
# below has to see it.
target_compile_definitions(think_objects PUBLIC THINK_STATIC_PLUGINS)

# ---------------------------------------------------------------------------
# The table
# ---------------------------------------------------------------------------

# One row per plugin the two think_add_ functions were handed, naming the
# functions its namespace defines and the version byte its ABI is gated on.
# A plugin whose functions do not match these declarations fails at the
# link, which is the right place.
get_property(STATIC_PLUGINS   GLOBAL PROPERTY THINK_STATIC_PLUGINS)
get_property(STATIC_COMPOSERS GLOBAL PROPERTY THINK_STATIC_COMPOSERS)
get_property(STATIC_SOURCES   GLOBAL PROPERTY THINK_STATIC_SOURCES)

set(decls "")
set(syms "")
set(rows "")
set(composer_names "")

foreach(p IN LISTS STATIC_PLUGINS)
  string(REPLACE "/" "_" id "${p}")
  string(APPEND decls
"namespace thp_${id} {
    int  module_init (thPlugin *);
    int  module_callback (thNode *, thSynthTree *, unsigned int, unsigned int);
    void module_cleanup (thPlugin *);
}
")
  string(APPEND syms
"const thStaticSymbol dsp_${id}[] = {
    { \"apiversion\",      &dspVersion_ },
    { \"module_init\",     (void *)&thp_${id}::module_init },
    { \"module_callback\", (void *)&thp_${id}::module_callback },
    { \"module_cleanup\",  (void *)&thp_${id}::module_cleanup },
};
")
  string(APPEND rows "    TH_ROW(\"${p}\", dsp_${id}),\n")
endforeach()

foreach(c IN LISTS STATIC_COMPOSERS)
  get_property(exports GLOBAL PROPERTY THINK_STATIC_COMPOSER_${c})

  string(APPEND decls "extern \"C\" {\n")
  string(APPEND syms
"const thStaticSymbol com_${c}[] = {
    { \"composer_apiversion\", &composerVersion_ },
")

  foreach(ex IN LISTS exports)
    string(REPLACE "@" "thc_${c}_${ex}" proto "${THINK_COMPOSER_PROTO_${ex}}")
    string(APPEND decls "    ${proto};\n")
    string(APPEND syms
           "    { \"composer_${ex}\", (void *)&thc_${c}_${ex} },\n")
  endforeach()

  string(APPEND decls "}\n")
  string(APPEND syms "};\n")
  string(APPEND rows "    TH_ROW(\"composer/${c}\", com_${c}),\n")
  string(APPEND composer_names "    \"composer/${c}\",\n")
endforeach()

file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/static/registry.cpp" CONTENT
"/* The table thDynLib's static branch reads: every DSP plugin and every
   composer in plugins/CMakeLists.txt. Written by wasm/web/CMakeLists.txt. */
#include \"thinkstatic.h\"
#include \"thcomposer.h\"
#include \"thDynLib.h\"

namespace {

/* What thPlugin and thcPlugin check a plugin's version byte against before
   they call anything in it. A plugin compiled into this module was compiled
   against these headers, so these are its. */
unsigned char dspVersion_ = MODULE_IFACE_VER;
unsigned char composerVersion_ = COMPOSER_IFACE_VER;

} /* namespace */

${decls}
namespace {

${syms}
#define TH_ROW(name, syms) { name, syms, sizeof(syms) / sizeof(syms[0]) }

} /* namespace */

const thStaticPlugin thStaticPlugins[] = {
${rows}};

const size_t thStaticPluginCount =
    sizeof(thStaticPlugins) / sizeof(thStaticPlugins[0]);

const char *const thStaticComposers[] = {
${composer_names}};

const size_t thStaticComposerCount =
    sizeof(thStaticComposers) / sizeof(thStaticComposers[0]);
")

# ---------------------------------------------------------------------------
# The module
# ---------------------------------------------------------------------------

# The composer host is src/CMakeLists.txt's think_composerhost, borrowed the
# way wasm/CMakeLists.txt borrows it and for the same reason: that file
# builds the application. shim/ stands in for glibmm -- see wasm/shim/
# glibmm.h; the scheduler asks glib for a monotonic clock it does not use
# here and hangs a timeout that never fires, because the audio clock steps
# it (JAM.md section 3, and SCHEDULER_PLACEMENT.md for why here and not on
# the main thread).
add_executable(thinkweb
    ${STATIC_SOURCES}
    "${PROJECT_BINARY_DIR}/static/registry.cpp"
    "${THINK_TOP}/src/gthSynthSource.cpp"
    "${THINK_TOP}/src/thcPlugin.cpp"
    "${THINK_TOP}/src/thcScheduler.cpp"
    "${THINK_TOP}/src/thcNodeHost.cpp"
    "${THINK_TOP}/src/thcGenFile.cpp"
    "${THINK_TOP}/src/thcGenEdit.cpp"
    "${THINK_TOP}/wasm/glib.cpp"
    thinkweb.cpp)

target_include_directories(thinkweb PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THINK_TOP}/wasm"
    "${THINK_TOP}/wasm/shim"
    "${THINK_TOP}"
    "${THINK_TOP}/src")

# The composers draw nowhere; ThinkPlugin.cmake here says what that costs
# and what it buys.
target_compile_definitions(thinkweb PRIVATE THC_NO_DRAW)

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
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,HEAPF32,HEAPF64,HEAP32,HEAPU32)

# ---------------------------------------------------------------------------
# The site
# ---------------------------------------------------------------------------

# The page and its scripts, copied beside the module so the build directory
# is a site on its own: serve it, or put it anywhere that serves files.
#
# tape.mjs is wasm/'s, not this directory's: the worklet reads the tape out
# of the heap with the same code genwav.mjs does, which is what makes
# comparing the two tapes a claim about the piece (wasm/twevent.h).
set(SITE_SOURCES index.html main.js host.js worklet.js ../tape.mjs)
set(SITE_OUTPUTS "")

foreach(f IN LISTS SITE_SOURCES)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${PROJECT_BINARY_DIR}/${name}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/${name}")
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

# The shipped pieces, and their list. The page offers these; the worklet
# hands the .dsp files above to the module before it loads one, since an
# instrument is a .dsp looked up by name and a worklet cannot fetch.
file(GLOB SHIPPED_GEN "${THINK_TOP}/gen/*.gen")
list(SORT SHIPPED_GEN)

set(gen_names "")

foreach(f IN LISTS SHIPPED_GEN)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/gen/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${f}" "${PROJECT_BINARY_DIR}/gen/${name}"
      DEPENDS "${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/gen/${name}")
  list(APPEND gen_names "\"${name}\"")
endforeach()

list(JOIN gen_names ", " gen_names)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/gen/index.json"
     CONTENT "[${gen_names}]\n")

add_custom_target(site ALL DEPENDS ${SITE_OUTPUTS})
