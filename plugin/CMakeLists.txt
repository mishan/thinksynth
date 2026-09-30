# thinksynth as an audio plugin: dsp/juno.dsp, compiled in, as a CLAP, a
# VST3 and an LV2 instrument. See ThinkPlugin.cpp.
#
#   cmake -S . -B build -DTHINK_BUILD_PLUGIN=ON
#   cmake --build build --target thinksynth-juno-clap thinksynth-juno-vst3 \
#                                thinksynth-juno-lv2
#
# The bundles land in build/bin/: thinksynth-juno.clap, thinksynth-juno.vst3/
# and thinksynth-juno.lv2/. A host finds them in ~/.clap, ~/.vst3 and ~/.lv2
# (symlink them there), or wherever it is told to scan.
#
# DPF (https://github.com/DISTRHO/DPF, ISC) is what turns one plugin class
# into the three formats; it carries its own VST3 implementation, so the
# Steinberg SDK is not a dependency. Pinned to a commit: DPF has no tags,
# and a plugin's identity in a saved project is not a thing to let drift
# with someone else's default branch. Its one submodule, pugl, is the window
# the editor is drawn in; on Linux that wants X11, Xcursor, Xext, Xrandr,
# D-Bus and cairo (libx11-dev libxcursor-dev libxext-dev libxrandr-dev
# libdbus-1-dev libcairo2-dev).

include(FetchContent)

# One patch on top, dpf-clap-latency.patch: DPF's CLAP wrapper starts from
# a known latency of 0, so a plugin whose latency is a constant it set in
# its constructor is reported to the host as having changed it -- at the
# first deactivate or state load, which CLAP forbids outside activate and
# clap-validator fails. The patch starts it from the plugin's own, and
# ApplyPatch.cmake applies it once however often FetchContent asks.
FetchContent_Declare(dpf
    GIT_REPOSITORY https://github.com/DISTRHO/DPF.git
    GIT_TAG        4238e1c7f0351bbe488d79f0899c540543ac7583
    GIT_SUBMODULES "dgl/src/pugl-upstream"
    PATCH_COMMAND  "${CMAKE_COMMAND}"
                   "-DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/dpf-clap-latency.patch"
                   -P "${CMAKE_CURRENT_SOURCE_DIR}/ApplyPatch.cmake")

FetchContent_MakeAvailable(dpf)

# The graph, as a string the plugin loads with loadTreeText. Read at
# configure time and re-read when the file changes.
set(THINK_PLUGIN_DSP "${PROJECT_SOURCE_DIR}/dsp/juno.dsp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${THINK_PLUGIN_DSP}")

file(READ "${THINK_PLUGIN_DSP}" dsp_text)

if(dsp_text MATCHES "\\)thinksynth\"")
  message(FATAL_ERROR "${THINK_PLUGIN_DSP} contains the raw string's closing "
                      "delimiter, )thinksynth\"")
endif()

get_filename_component(dsp_name "${THINK_PLUGIN_DSP}" NAME)

# The description statement: at the start of a line, so not one a comment
# mentions, and escaped for a C string.
string(REGEX MATCH "(^|\n)[ \t]*description[ \t]+\"([^\"\n]*)\"" _
       "${dsp_text}")
string(REPLACE "\\" "\\\\" dsp_description "${CMAKE_MATCH_2}")

# `@' and `${' in a .dsp are text, not things for file(CONFIGURE) to expand,
# so the header is assembled here and written with file(WRITE) -- beside
# it, and copied over it only if it differs, so a reconfigure that changed
# nothing recompiles nothing.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/thinksynth_dsp.h.new"
"/* ${dsp_name}, embedded. Written by plugin/CMakeLists.txt. */
static const char thPluginDspName[] = \"${dsp_name}\";
static const char thPluginDspLabel[] = \"thinksynth_juno\";
static const char thPluginDspDescription[] = \"${dsp_description}\";
static const char thPluginDspText[] = R\"thinksynth(${dsp_text})thinksynth\";
")

configure_file("${CMAKE_CURRENT_BINARY_DIR}/thinksynth_dsp.h.new"
               "${CMAKE_CURRENT_BINARY_DIR}/thinksynth_dsp.h" COPYONLY)

# The controls, read off the engine at build time (controlsgen.cpp) into a
# table the plugin and its editor both compile in, and the editor's size
# into the header DistrhoPluginInfo.h takes DPF's default UI size from.
# So only the DSP side links the engine: an LV2 editor, a module of its
# own, carries none of it.
add_executable(controlsgen controlsgen.cpp ReadControls.cpp Panel.cpp)
target_include_directories(controlsgen PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(controlsgen PRIVATE think_embedded PkgConfig::CAIRO)

set(THINK_PLUGIN_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/thinksynth_controls.h"
    "${CMAKE_CURRENT_BINARY_DIR}/thinksynth_ui_size.h")

add_custom_command(
    OUTPUT ${THINK_PLUGIN_GENERATED}
    COMMAND controlsgen ${THINK_PLUGIN_GENERATED}
    DEPENDS controlsgen
    COMMENT "Reading juno.dsp's controls")

add_custom_target(thinksynth-juno-controls DEPENDS ${THINK_PLUGIN_GENERATED})

dpf_add_plugin(thinksynth-juno
    TARGETS clap vst3 lv2
    UI_TYPE cairo
    FILES_COMMON Controls.cpp
    FILES_DSP ThinkPlugin.cpp
    FILES_UI Panel.cpp ThinkUI.cpp)

target_include_directories(thinksynth-juno PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(thinksynth-juno-dsp PUBLIC think_embedded)

# Everything DPF made here includes DistrhoPluginInfo.h, and with it the
# generated size.
foreach(t thinksynth-juno thinksynth-juno-dsp thinksynth-juno-ui
          thinksynth-juno-clap thinksynth-juno-vst3 thinksynth-juno-lv2
          thinksynth-juno-lv2-ui)
  if(TARGET ${t})
    add_dependencies(${t} thinksynth-juno-controls)
  endif()
endforeach()

# DPF's LV2 generator, which runs the plugin at build time to write its
# .ttl, casts away a const GCC warns about.
target_compile_options(lv2_ttl_generator PRIVATE -Wno-discarded-qualifiers)

# DPF's own sources as DPF builds them on its own, in C++11: its pugl
# wrapper, which its window library and the plugin entry points both
# compile, includes <cstdbool>, and under this tree's C++17 that warns.
# The entry points' only other include is DistrhoPluginInfo.h.
foreach(t dgl-cairo thinksynth-juno-clap thinksynth-juno-vst3
          thinksynth-juno-lv2 thinksynth-juno-lv2-ui)
  if(TARGET ${t})
    set_target_properties(${t} PROPERTIES CXX_STANDARD 11)
  endif()
endforeach()

# And its file browser, which this plugin does not open and pugl.cpp
# compiles in, without the one warning GCC has about its fixed-size path
# buffers.
target_compile_options(dgl-cairo PRIVATE -Wno-format-overflow)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# The editor with no window: see uicheck.cpp. It writes what it drew to
# editor.png beside itself.
add_executable(uicheck uicheck.cpp Controls.cpp Panel.cpp)
target_include_directories(uicheck PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(uicheck PRIVATE PkgConfig::CAIRO)
add_dependencies(uicheck thinksynth-juno-controls)

add_test(NAME uicheck
         COMMAND uicheck "${CMAKE_CURRENT_BINARY_DIR}/editor.png")

# The built CLAP against the engine: see plugincheck.cpp. dlopen, so Unix.
if(NOT WIN32)
  add_executable(plugincheck plugincheck.cpp Controls.cpp ReadControls.cpp)
  target_include_directories(plugincheck PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}"
      "${dpf_SOURCE_DIR}/distrho/src")
  target_link_libraries(plugincheck PRIVATE think_embedded ${CMAKE_DL_LIBS})
  target_include_directories(plugincheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(plugincheck thinksynth-juno-controls)
  add_dependencies(plugincheck thinksynth-juno-clap)

  # The editor through a host: see guicheck.cpp. X11 and its XTest
  # extension (libxtst-dev), so Linux, and only where XTest is there.
  find_package(X11)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT X11_Xtst_FOUND)
    message(STATUS "plugin: no XTest (libxtst-dev), so no guicheck")
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_Xtst_FOUND)

    add_executable(guicheck guicheck.cpp Controls.cpp Panel.cpp)
    target_include_directories(guicheck PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${dpf_SOURCE_DIR}/distrho/src")
    target_link_libraries(guicheck PRIVATE X11::X11 X11::Xtst PkgConfig::CAIRO
                                           ${CMAKE_DL_LIBS})
    add_dependencies(guicheck thinksynth-juno-clap thinksynth-juno-controls)

    add_test(NAME guicheck
             COMMAND guicheck "${PROJECT_BINARY_DIR}/bin/thinksynth-juno.clap")
    set_tests_properties(guicheck PROPERTIES
                         SKIP_REGULAR_EXPRESSION "SKIP  no display")
  endif()

  add_test(NAME plugincheck
           COMMAND plugincheck "${PROJECT_BINARY_DIR}/bin/thinksynth-juno.clap")
endif()

# The formats' own validators, run on what was built: clap-validator on the
# CLAP, pluginval on the VST3 at its strictest, its editor tests included --
# it opens the editor and automates it. Linux builds of both, fetched at
# pinned releases, so Linux only. CMAKE_TEST_LAUNCHER -- scripts/headless.sh
# -- reaches only a test whose command is a target this build makes, and
# these two are fetched binaries named by path, so it goes on them by hand:
# it is what keeps the editor on a private display. The one warning
# clap-validator gives -- state that loads from random bytes -- is DPF's
# state reader, not this plugin.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  FetchContent_Declare(clapvalidator
      URL https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip
      URL_HASH SHA256=49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  FetchContent_Declare(pluginval
      URL https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip
      URL_HASH SHA256=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  FetchContent_MakeAvailable(clapvalidator pluginval)

  # The release zip holds a tarball, which holds the binary.
  file(GLOB clapvalidator_tarball "${clapvalidator_SOURCE_DIR}/*.tar.gz")
  set(clapvalidator_bin "${CMAKE_CURRENT_BINARY_DIR}/clap-validator")

  if(clapvalidator_tarball AND NOT EXISTS "${clapvalidator_bin}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xzf ${clapvalidator_tarball}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  add_test(NAME plugin.clap-validator
           COMMAND ${CMAKE_TEST_LAUNCHER} "${clapvalidator_bin}" validate
                   "${PROJECT_BINARY_DIR}/bin/thinksynth-juno.clap")

  add_test(NAME plugin.pluginval
           COMMAND ${CMAKE_TEST_LAUNCHER} "${pluginval_SOURCE_DIR}/pluginval"
                   --strictness-level 10
                   --validate-in-process
                   --validate "${PROJECT_BINARY_DIR}/bin/thinksynth-juno.vst3")

  set_tests_properties(plugin.pluginval PROPERTIES TIMEOUT 900)
endif()
