# thinksynth for the browser: libthink, every DSP plugin and every composer,
# with the composer host, linked into one wasm module for an AudioWorklet,
# and the page that plays it: the synth in a tab, and then the piece in a
# tab, which is the scheduler running in the worklet too (docs/JAM.md).
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
# main.js, worklet.js, thinkweb.js and .wasm, the shipped .dsp, .gen and
# .patch files the page offers, each directory with an index.json listing
# it, and jam.html with its bundled jam.js -- the room page, which is the
# one thing here with dependencies (`npm ci' in this directory, once).
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
# composer_draw is among them now. It used to be the one export that could
# not come this way -- a worklet has no canvas and there was no cairo to
# link -- and what changed is cairo-canvas2d, which is cairo's API over a
# display list the page replays. The composers draw through that,
# unchanged.
set(THINK_COMPOSER_EXPORTS init create tick receive param_changed input
                           capture destroy draw)

set(THINK_COMPOSER_PROTO_init          "int @ (thcComposerInfo *)")
set(THINK_COMPOSER_PROTO_create        "void *@ (const thcParams *)")
set(THINK_COMPOSER_PROTO_tick
    "double @ (void *, const thcTransport *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_receive
    "void @ (void *, const thcEvent *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_param_changed "void @ (void *, int)")
set(THINK_COMPOSER_PROTO_input         "void @ (void *, const thcInputEvent *)")
set(THINK_COMPOSER_PROTO_capture       "const char *@ (void *, int)")
set(THINK_COMPOSER_PROTO_draw
    "void @ (void *, cairo_t *, double, double)")
set(THINK_COMPOSER_PROTO_destroy       "void @ (void *)")

# And what a visual module may export, with the same treatment: a probe's
# display is a module like any other (src/thVisual.h), and the page opens
# one per armed probe.
set(THINK_VISUAL_EXPORTS init open feed draw close cleanup)

set(THINK_VISUAL_PROTO_init    "int @ (thVisual *)")
set(THINK_VISUAL_PROTO_open    "void *@ (thVisual *, unsigned int)")
set(THINK_VISUAL_PROTO_feed    "int @ (void *, const float *, unsigned int)")
set(THINK_VISUAL_PROTO_draw    "int @ (void *, cairo_t *, int, int)")
set(THINK_VISUAL_PROTO_close   "void @ (void *)")
set(THINK_VISUAL_PROTO_cleanup "void @ (thVisual *)")

# cairo, for the composers' draws and for the canvases later, as a display
# list the page replays on a Canvas2D. It is cairo-canvas2d, from npm, and
# thinksynth uses it the way anyone would: the package on the include
# path, and nothing of this tree on its.
#
# Both halves come from the one package because they have to agree. The
# opcodes are written down twice -- once in cairo2d.h, once in replay.js
# -- and they carry the arity, so a header a version away from its
# replayer does not fail to load, it reads operands as opcodes and draws
# rubbish from the changed op onwards. One version number is what stops
# that, and it is the lockfile's.
#
# Its test is built here because this is the build with a node beside it,
# and CI runs it -- against the pinned copy, which is the one that matters
# to this build rather than whatever upstream's own CI last saw.
set(CAIRO2D "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/cairo-canvas2d")

if(NOT EXISTS "${CAIRO2D}/CMakeLists.txt")
  message(FATAL_ERROR
      "cairo-canvas2d is not installed: run `npm ci` in wasm/web first.\n"
      "The composers' cairo comes from npm, so the browser build needs "
      "node_modules before it can configure.")
endif()

set(CAIRO2D_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("${CAIRO2D}" cairo2d)

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
get_property(STATIC_VISUALS   GLOBAL PROPERTY THINK_STATIC_VISUALS)
get_property(STATIC_SOURCES   GLOBAL PROPERTY THINK_STATIC_SOURCES)

set(decls "")
set(syms "")
set(rows "")
set(composer_names "")
set(visual_names "")

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

# The visuals, exactly as the composers: every export declared weak for
# every module, so what the module compiled decides which it has.
foreach(v IN LISTS STATIC_VISUALS)
  string(APPEND decls "extern \"C\" {\n")
  string(APPEND syms
"const thStaticSymbol vis_${v}[] = {
    { \"visual_apiversion\", &visualVersion_ },
")

  foreach(ex IN LISTS THINK_VISUAL_EXPORTS)
    string(REPLACE "@" "thv_${v}_${ex}" proto "${THINK_VISUAL_PROTO_${ex}}")
    string(APPEND decls "    ${proto} __attribute__((weak));\n")
    string(APPEND syms
           "    { \"visual_${ex}\", (void *)&thv_${v}_${ex} },\n")
  endforeach()

  string(APPEND decls "}\n")
  string(APPEND syms "};\n")
  string(APPEND rows "    TH_ROW(\"visual/${v}\", vis_${v}),\n")
  string(APPEND visual_names "    \"visual/${v}\",\n")
endforeach()

file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/static/registry.cpp" CONTENT
"/* The table thDynLib's static branch reads: every DSP plugin and every
   composer in plugins/CMakeLists.txt. Written by wasm/web/CMakeLists.txt. */
#include \"thinkstatic.h\"
#include \"thcomposer.h\"
#include \"thVisual.h\"
#include \"thDynLib.h\"

namespace {

/* What thPlugin and thcPlugin check a plugin's version byte against before
   they call anything in it. A plugin compiled into this module was compiled
   against these headers, so these are its. */
unsigned char dspVersion_ = MODULE_IFACE_VER;
unsigned char composerVersion_ = COMPOSER_IFACE_VER;
unsigned char visualVersion_ = VISUAL_IFACE_VER;

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

const char *const thStaticVisuals[] = {
${visual_names}};

const size_t thStaticVisualCount =
    sizeof(thStaticVisuals) / sizeof(thStaticVisuals[0]);
")

# ---------------------------------------------------------------------------
# The module
# ---------------------------------------------------------------------------

# The composer host is ../cmake/ThinkEmscripten.cmake's list, shared with
# the Node build. shim/ stands in for glibmm -- see wasm/shim/glibmm.h; the
# scheduler asks glib for a monotonic clock it does not use here and hangs a
# timeout that never fires, because the audio clock steps it rather than the
# main thread (docs/JAM.md). The composer canvas is the desktop's, compiled
# again here: one drawing, one behaviour, two shells. CanvasContent is its
# base; what it draws through is cairo2d's cairomm face, and what it never
# sees is a toolkit -- the first Gtk:: that got into either would fail this
# build, which is the guard the split exists for.
#
# PanelModel and ArgPanel are the same move for a parameter panel: what a
# control is, in as many words as it takes and none of them a widget
# (src/PanelModel.h). The guard is this line -- they are built here, and
# nothing here links a toolkit.
add_executable(thinkweb
    ${STATIC_SOURCES}
    "${PROJECT_BINARY_DIR}/static/registry.cpp"
    "${THINK_TOP}/src/gthSynthSource.cpp"
    "${THINK_TOP}/src/thVisual.cpp"
    "${THINK_TOP}/src/CanvasContent.cpp"
    "${THINK_TOP}/src/ComposerCanvas.cpp"
    "${THINK_TOP}/src/NodeCanvas.cpp"
    "${THINK_TOP}/src/NodeGraph.cpp"
    "${THINK_TOP}/src/NodeEdit.cpp"
    "${THINK_TOP}/src/NodeLayout.cpp"
    "${THINK_TOP}/src/NodeCatalog.cpp"
    "${THINK_TOP}/src/PanelModel.cpp"
    "${THINK_TOP}/src/ArgPanel.cpp"
    "${THINK_TOP}/src/KnobPanel.cpp"
    ${THINK_COMPOSERHOST_SOURCES}
    twdraw.cpp
    thinkweb.cpp
    thinknode.cpp)

target_include_directories(thinkweb PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THINK_TOP}/wasm"
    "${THINK_TOP}/wasm/shim"
    "${THINK_TOP}"
    "${THINK_TOP}/src")

target_link_libraries(thinkweb PRIVATE think_objects sigc cairo2d)

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
set(SITE_SOURCES index.html jam.html style.css panes.css main.js keyboard.js
                keyfocus.js panel.js patch.js host.js roll.js worklet.js
                engine.js mirror.js canvasview.js composerview.js nodeview.js
                panes.js tapediff.js)
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

# And the replayer, which is cairo-canvas2d's: the other half of the cairo
# stand-in the module draws through, and the only part of it that is
# JavaScript. It keeps its name here, where the page loads it by name, and
# is served beside the page that replays with it.
add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/replay.js"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CAIRO2D}/replay.js"
            "${PROJECT_BINARY_DIR}/replay.js"
    DEPENDS "${CAIRO2D}/replay.js")
list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/replay.js")

# The shipped DSPs -- dsp/*.dsp -- and the list the page's menu is filled
# from. Patch mode plays one of these directly; a piece's instruments and a
# .patch's `dsp' line are both looked up among them. This glob skipped the
# effects/, noargs/ and old/ drawers for years while the desktop install
# shipped them; the drawers have since gone, so the two agree by there being
# nothing left to disagree about.
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

# The effect graphs -- dsp/fx/*.dsp -- go into the same list, as
# `fx/<name>', which is how a piece's `effect' clause spells them and how
# the loader looks them up under dsp/. The page hands every name in the
# list to the worklet, so an effect reaches the module the way an
# instrument does; what treats the list as playable patches -- the patch
# menu, check.mjs -- skips the `fx/' entries, since an effect is not a
# note-playing graph (docs/DSP_FORMAT.md, "An effect graph").
file(GLOB SHIPPED_FX "${THINK_TOP}/dsp/fx/*.dsp")
list(SORT SHIPPED_FX)

foreach(f IN LISTS SHIPPED_FX)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/dsp/fx/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${f}" "${PROJECT_BINARY_DIR}/dsp/fx/${name}"
      DEPENDS "${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/dsp/fx/${name}")
  list(APPEND dsp_names "\"fx/${name}\"")
endforeach()

# And the kit -- dsp/samples/*.wav -- as `samples/<name>', which is how
# osc::sample's `file' arg is looked up (thUtil::findDataFile under
# `samples/'). They go in the same list for the same reason the effect
# graphs do: the page hands every name in it to the worklet, and a
# worklet cannot fetch. What differs is that these are bytes rather than
# text, so main.js fetches them as ArrayBuffers and hands them over
# through tw_sample rather than tw_instrument, and everything that treats
# the list as playable patches skips them along with the `fx/' entries.
#
# dsp/samples/ is generated -- scripts/makekit.sh renders the tree's own
# drum graphs into it -- so a tree that has never run it simply has no
# samples here, and the glob is empty rather than wrong.
file(GLOB SHIPPED_SAMPLES "${THINK_TOP}/dsp/samples/*.wav")
list(SORT SHIPPED_SAMPLES)

foreach(f IN LISTS SHIPPED_SAMPLES)
  get_filename_component(name "${f}" NAME)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/dsp/samples/${name}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${f}" "${PROJECT_BINARY_DIR}/dsp/samples/${name}"
      DEPENDS "${f}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/dsp/samples/${name}")
  list(APPEND dsp_names "\"samples/${name}\"")
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

# The shipped patches, the whole tree -- a .patch lives in a drawer named
# for what it is, `leads/SuperRes.patch', and the relative name is the name
# the desktop's thinkrc uses and the one the index lists. Recursive, unlike
# the .dsp glob above, because for these the drawers are the content.
#
# What the page does with them: a piece that names a channel and declares
# no instrument for it is asking the reader to aim it, and the page aims it
# at gthPrefs.cpp's first-run patch for that channel (patch.js). The .dsp a
# patch names is one of the ones copied above; nothing here fetches a .dsp
# of its own.
file(GLOB_RECURSE SHIPPED_PATCH RELATIVE "${THINK_TOP}/patches"
     "${THINK_TOP}/patches/*.patch")

# A .patch loose at the top of the tree is not one of the shipped sounds:
# patches/test.patch is a 2004 fixture by J. Random Hacker, and offering it
# in the page's menus as a bare "test" above the drawers is the same kind of
# noise dsp/'s old drawers were. The drawer is the content here, so the top
# level is what gets dropped rather than what gets kept -- which is the
# opposite of how it went for the DSPs, where the drawers were what went and
# the top level is what stayed.
list(FILTER SHIPPED_PATCH EXCLUDE REGEX "^[^/]+$")
list(SORT SHIPPED_PATCH)

set(patch_names "")

foreach(rel IN LISTS SHIPPED_PATCH)
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/patches/${rel}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${THINK_TOP}/patches/${rel}"
              "${PROJECT_BINARY_DIR}/patches/${rel}"
      DEPENDS "${THINK_TOP}/patches/${rel}")
  list(APPEND SITE_OUTPUTS "${PROJECT_BINARY_DIR}/patches/${rel}")
  list(APPEND patch_names "\"${rel}\"")
endforeach()

list(JOIN patch_names ", " patch_names)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/patches/index.json"
     CONTENT "[${patch_names}]\n")

# The room page's script is bundled: it has an editor, a CRDT and a
# provider, which come from node_modules and go nowhere a browser could
# fetch them from. So the room page wants `npm ci' run in this directory
# first, once; the solo page does not -- and a tree without node_modules
# configures and builds the solo page rather than refusing to configure at
# all.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/esbuild")
  message(STATUS
    "wasm/web: no node_modules, so no room page. Run `npm ci' in "
    "${CMAKE_CURRENT_SOURCE_DIR} and configure again to build it; the "
    "solo page does not need it.")
  set(JAM_DIST_FILES "")
else()
  # package-lock.json among the dependencies because the bundle contains
  # what it pins: a dependency bumped without it here would rebuild
  # nothing and serve the old editor.
  set(JAM_SOURCES jam.js room.js mesh.js editor.js doc.js clock.js commands.js
                  roll.js host.js keyboard.js panel.js patch.js ../tape.mjs
                  canvasview.js composerview.js nodeview.js panes.js
                  tapediff.js bundle.mjs package-lock.json)
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

  # For dist below, which must not install what was never built. The map
  # goes too: it is fetched only when somebody opens the devtools, and a
  # phone is where that somebody most wants it.
  set(JAM_DIST_FILES "${PROJECT_BINARY_DIR}/jam.js"
                     "${PROJECT_BINARY_DIR}/jam.js.map")
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
# that is where the copies and the bundle are. The room page's two are in
# JAM_DIST_FILES, which is empty when there was no node_modules to bundle
# it from: dist installs what was built and nothing else.
set(DIST_ROOT_FILES "")
foreach(f IN LISTS SITE_SOURCES)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_ROOT_FILES "${PROJECT_BINARY_DIR}/${name}")
endforeach()
list(APPEND DIST_ROOT_FILES
    "${PROJECT_BINARY_DIR}/tape.js"
    "${PROJECT_BINARY_DIR}/replay.js"
    ${JAM_DIST_FILES}
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

set(DIST_FX_FILES "")
foreach(f IN LISTS SHIPPED_FX)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_FX_FILES "${PROJECT_BINARY_DIR}/dsp/fx/${name}")
endforeach()
install(FILES ${DIST_FX_FILES} DESTINATION dsp/fx PERMISSIONS ${SITE_FILE_MODE})

# And the kit. dsp/index.json names these the same way it names a graph, so
# the page asks the server for every one of them whatever is installed --
# and fetch() does not reject on a 404, so a missing wav arrives as the
# error page's bytes and osc::sample is handed something that is not a
# RIFF. Nothing throws; the instruments that play a recording just stop
# making a sound, which is a long way to look from the symptom.
set(DIST_SAMPLE_FILES "")
foreach(f IN LISTS SHIPPED_SAMPLES)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_SAMPLE_FILES "${PROJECT_BINARY_DIR}/dsp/samples/${name}")
endforeach()

if(DIST_SAMPLE_FILES)
  install(FILES ${DIST_SAMPLE_FILES}
          DESTINATION dsp/samples PERMISSIONS ${SITE_FILE_MODE})
endif()

set(DIST_GEN_FILES "${PROJECT_BINARY_DIR}/gen/index.json")
foreach(f IN LISTS SHIPPED_GEN)
  get_filename_component(name "${f}" NAME)
  list(APPEND DIST_GEN_FILES "${PROJECT_BINARY_DIR}/gen/${name}")
endforeach()
install(FILES ${DIST_GEN_FILES} DESTINATION gen PERMISSIONS ${SITE_FILE_MODE})

# And the patches: one install rule per drawer, since a destination is one
# directory, and an explicit list rather than the directory the build made.
# Installing that directory would be shorter and would also carry a stale
# file -- copy_if_different adds and never removes, so a .patch that has
# been renamed, or dropped from the glob as patches/test.patch was, sits
# there until somebody deletes the build directory. `the files a server
# needs and no others' has to mean the list, not the leftovers.
install(FILES "${PROJECT_BINARY_DIR}/patches/index.json"
        DESTINATION patches PERMISSIONS ${SITE_FILE_MODE})

set(DIST_PATCH_DIRS "")
foreach(rel IN LISTS SHIPPED_PATCH)
  get_filename_component(dir "${rel}" DIRECTORY)
  list(APPEND DIST_PATCH_DIRS "${dir}")
endforeach()
list(REMOVE_DUPLICATES DIST_PATCH_DIRS)

foreach(dir IN LISTS DIST_PATCH_DIRS)
  set(DIST_PATCH_FILES "")

  foreach(rel IN LISTS SHIPPED_PATCH)
    get_filename_component(d "${rel}" DIRECTORY)

    if(d STREQUAL dir)
      list(APPEND DIST_PATCH_FILES "${PROJECT_BINARY_DIR}/patches/${rel}")
    endif()
  endforeach()

  install(FILES ${DIST_PATCH_FILES}
          DESTINATION "patches/${dir}" PERMISSIONS ${SITE_FILE_MODE})
endforeach()

set(DIST_DIR "${PROJECT_BINARY_DIR}/dist")

add_custom_target(dist
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DIST_DIR}"
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
            --prefix "${DIST_DIR}"
    COMMENT "Packaging the site into ${DIST_DIR}"
    VERBATIM)
add_dependencies(dist site thinkweb)
