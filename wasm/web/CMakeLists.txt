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
# main.js, worklet.js, thinkweb.js and .wasm, the shipped .dsp and .gen
# files the page offers, each directory with an index.json listing it, and
# jam.html with its bundled jam.js -- the room page, M3, which is the one
# thing here with dependencies (`npm ci' in this directory, once).
#
# The build directory can be served as it is, and `serve.mjs' does. For
# putting it somewhere else there is
#
#   cmake --build build-web --target dist
#
# which installs the site, and nothing but the site, into build-web/dist/
# with every file 644 and every directory 755, removing what was there
# first. A copy of the build directory is not that: it carries the object
# files and CMake's own state, and a copy keeps each file's mode from the
# source tree, so a .dsp that is 640 there is 640 on the server and a 403
# to the browser.
#
# Every module served is a .js, the wasm's glue included: a .mjs is a
# module to a browser only if the server calls it JavaScript, and the
# common mime.types files do not, so a .mjs on a stock nginx is served as
# application/octet-stream and refused by Chrome (Firefox loads it with a
# warning, which is how that stays unnoticed on one browser). .js is in
# every mime.types there is.

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

# Every export a composer may have is declared for every composer, weak:
# the address of one the plugin did not define links as a null, and a null
# in the table is what thcPlugin reads as "not offered" -- the same answer
# dlopen gives for a module without the symbol. So which exports a composer
# has is decided by what it compiled, not by a scan of how it spelled them.
foreach(c IN LISTS STATIC_COMPOSERS)
  string(APPEND decls "extern \"C\" {\n")
  string(APPEND syms
"const thStaticSymbol com_${c}[] = {
    { \"composer_apiversion\", &composerVersion_ },
")

  foreach(ex IN LISTS THINK_COMPOSER_EXPORTS)
    string(REPLACE "@" "thc_${c}_${ex}" proto "${THINK_COMPOSER_PROTO_${ex}}")
    string(APPEND decls "    ${proto} __attribute__((weak));\n")
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

# The composer host is ../cmake/ThinkEmscripten.cmake's list, shared with
# the Node build. shim/ stands in for glibmm -- see wasm/shim/glibmm.h; the
# scheduler asks glib for a monotonic clock it does not use here and hangs
# a timeout that never fires, because the audio clock steps it (JAM.md
# section 3, and SCHEDULER_PLACEMENT.md for why here and not on the main
# thread).
add_executable(thinkweb
    ${STATIC_SOURCES}
    "${PROJECT_BINARY_DIR}/static/registry.cpp"
    "${THINK_TOP}/src/gthSynthSource.cpp"
    ${THINK_COMPOSERHOST_SOURCES}
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

# .js, not Emscripten's .mjs, for the reason at the top; EXPORT_ES6 below
# is what makes it a module, whatever it is called.
set_target_properties(thinkweb PROPERTIES SUFFIX ".js")

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
set(SITE_SOURCES index.html jam.html style.css main.js keyboard.js knobs.js
                host.js roll.js worklet.js)
set(SITE_OUTPUTS "")

foreach(f IN LISTS SITE_SOURCES)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${PROJECT_BINARY_DIR}/${name}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/${name}")
endforeach()

# tape.mjs is wasm/'s, not this directory's: the worklet reads the tape out
# of the heap with the same code genwav.mjs does, which is what makes
# comparing the two tapes a claim about the piece (wasm/twevent.h). It
# keeps its name there, where Node reads it, and is served as tape.js.
add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/tape.js"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../tape.mjs" "${PROJECT_BINARY_DIR}/tape.js"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tape.mjs")
list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/tape.js")

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

# The room page's script is bundled (JAM_M3.md, section 4.2): it has an
# editor, a CRDT and a provider, which come from node_modules and go
# nowhere a browser could fetch them from. So the room page wants `npm ci'
# run in this directory first, once; the solo page does not -- and a tree
# without node_modules configures and builds the solo page rather than
# refusing to configure at all.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/esbuild")
  message(STATUS
    "wasm/web: no node_modules, so no room page. Run `npm ci' in "
    "${CMAKE_CURRENT_SOURCE_DIR} and configure again to build it; the "
    "solo page does not need it.")
else()
  # package-lock.json among the dependencies because the bundle contains
  # what it pins: a dependency bumped without it here would rebuild
  # nothing and serve the old editor.
  set(JAM_SOURCES jam.js room.js mesh.js editor.js doc.js clock.js commands.js
                  roll.js host.js keyboard.js knobs.js ../tape.mjs bundle.mjs
                  package-lock.json)
  set(JAM_DEPENDS "")

  foreach(f IN LISTS JAM_SOURCES)
    list(APPEND JAM_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
  endforeach()

  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/jam.js"
      COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
              "${PROJECT_BINARY_DIR}"
      DEPENDS ${JAM_DEPENDS}
      COMMENT "Bundling the room page")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/jam.js")
endif()

# Where the relay is, for a site served from anywhere. serve.mjs --relay
# answers this itself; a static host edits the file.
if(NOT EXISTS "${PROJECT_BINARY_DIR}/config.json")
  file(WRITE "${PROJECT_BINARY_DIR}/config.json" "{ \"relay\": null }\n")
endif()

add_custom_target(site ALL DEPENDS ${SITE_OUTPUTS})

# ---------------------------------------------------------------------------
# dist
# ---------------------------------------------------------------------------

# The site, installed: the files a server needs and no others, each with
# the mode a web server can read whatever the source tree's was. The
# install rules use the prefix as the site root, so
# `cmake --install build-web --prefix DIR' puts index.html at DIR/index.html.
# The dist target below is that, into build-web/dist/, after removing
# whatever an earlier dist left, so a stale file cannot ride along.

set(SITE_FILE_MODE OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

# The pages, their scripts and the bundle, from the build directory, since
# that is where the copies and the bundle are. jam.js.map goes too: it is
# fetched only when someone opens the devtools, and a phone is where that
# someone most wants it.
set(DIST_ROOT_FILES "")
foreach(f IN LISTS SITE_SOURCES)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_ROOT_FILES "${PROJECT_BINARY_DIR}/${name}")
endforeach()
list(APPEND DIST_ROOT_FILES
    "${PROJECT_BINARY_DIR}/tape.js"
    "${PROJECT_BINARY_DIR}/jam.js"
    "${PROJECT_BINARY_DIR}/jam.js.map"
    "${PROJECT_BINARY_DIR}/config.json"
    "$<TARGET_FILE:thinkweb>"
    "${PROJECT_BINARY_DIR}/thinkweb.wasm")

install(FILES ${DIST_ROOT_FILES} DESTINATION . PERMISSIONS ${SITE_FILE_MODE})

# The shipped patches and pieces, from the build directory's copies, with
# their index.json: what the page's menus are filled from.
set(DIST_DSP_FILES "${PROJECT_BINARY_DIR}/dsp/index.json")
foreach(f IN LISTS SHIPPED_DSP)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_DSP_FILES "${PROJECT_BINARY_DIR}/dsp/${name}")
endforeach()
install(FILES ${DIST_DSP_FILES} DESTINATION dsp PERMISSIONS ${SITE_FILE_MODE})

set(DIST_GEN_FILES "${PROJECT_BINARY_DIR}/gen/index.json")
foreach(f IN LISTS SHIPPED_GEN)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_GEN_FILES "${PROJECT_BINARY_DIR}/gen/${name}")
endforeach()
install(FILES ${DIST_GEN_FILES} DESTINATION gen PERMISSIONS ${SITE_FILE_MODE})

set(DIST_DIR "${PROJECT_BINARY_DIR}/dist")

add_custom_target(dist
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
            --prefix "${DIST_DIR}"
    COMMENT "Packaging the site into ${DIST_DIR}"
    VERBATIM)
add_dependencies(dist site thinkweb)
