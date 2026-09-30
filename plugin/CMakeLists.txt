# thinksynth as audio plugins: a .dsp, compiled in, as a CLAP, a VST3 and
# an LV2 instrument. See ThinkPlugin.cpp.
#
#   cmake -S . -B build -DTHINK_BUILD_PLUGIN=ON
#   cmake -S . -B build -DTHINK_BUILD_PLUGIN=ON -DTHINK_PLUGIN_DSPS="juno;ebass"
#   cmake -S . -B build -DTHINK_BUILD_PLUGIN=ON -DTHINK_PLUGIN_DSPS=all
#   cmake --build build
#
# THINK_PLUGIN_DSPS names the graphs to build: a name in dsp/ (`juno' for
# dsp/juno.dsp), a path to a .dsp anywhere, or `all' for every instrument
# in dsp/. For each, the bundles land in build/bin/: thinksynth-ID.clap,
# thinksynth-ID.vst3/ and thinksynth-ID.lv2/, where ID is the file's name.
# A host finds them in ~/.clap, ~/.vst3 and ~/.lv2 (symlink them there), or
# wherever it is told to scan. scripts/thinksynth-export does this for one
# .dsp from anywhere.
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

set(THINK_PLUGIN_DSPS "juno" CACHE STRING
    "The .dsp files to build as plugins: names in dsp/, paths, or all")

# The generator: what one .dsp's plugin compiles in, read off the engine at
# build time (controlsgen.cpp). So only a plugin's DSP side links the
# engine -- an LV2 editor, a module of its own, carries none of it.
add_executable(controlsgen controlsgen.cpp ReadControls.cpp Panel.cpp)
target_include_directories(controlsgen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(controlsgen PRIVATE think_embedded PkgConfig::CAIRO)


# The four characters a VST3 host keys the plugin by, from `id': juno's
# are TsJu, which it had before there was more than one; any other is T and
# three from a hash of the id, over letters and digits. A clash between two
# built together fails the configure below.
function(think_plugin_unique_id id out)
  if(id STREQUAL "juno")
    set(${out} "TsJu" PARENT_SCOPE)
    return()
  endif()

  set(alphabet "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
  string(MD5 hash "thinksynth:${id}")
  set(code "T")

  foreach(k 0 4 8)
    string(SUBSTRING "${hash}" ${k} 4 hex)
    math(EXPR n "0x${hex} % 62")
    string(SUBSTRING "${alphabet}" ${n} 1 ch)
    string(APPEND code "${ch}")
  endforeach()

  set(${out} "${code}" PARENT_SCOPE)
endfunction()

# think_add_dsp_plugin(<path to a .dsp>)
#
# thinksynth-ID-clap, -vst3 and -lv2 for the graph, ID being its file name
# without .dsp. Its samples are looked for beside it in samples/ and then
# in dsp/samples/.
function(think_add_dsp_plugin dsp)
  get_filename_component(id "${dsp}" NAME_WE)
  get_filename_component(dir "${dsp}" DIRECTORY)

  if(NOT id MATCHES "^[A-Za-z0-9_]+$")
    message(FATAL_ERROR "plugin: ${dsp}: a plugin's id is its file name, and "
                        "`${id}' has more than letters, digits and _ in it")
  endif()

  set(target "thinksynth-${id}")
  set(gen "${CMAKE_CURRENT_BINARY_DIR}/${id}")
  file(MAKE_DIRECTORY "${gen}")

  # The name, the category and the unique id, for DistrhoPluginInfo.h.
  file(READ "${dsp}" text)
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")
  string(REGEX MATCH "(^|\n)[ \t]*name[ \t]+\"([^\"\n]*)\"" _ "${text}")
  set(name "${CMAKE_MATCH_2}")

  if(name STREQUAL "")
    set(name "${id}")
  endif()

  string(REPLACE "\\" "" name "${name}")
  string(REPLACE "\"" "" name "${name}")
  string(REGEX MATCH "(^|\n)[ \t]*category[ \t]+\"([^\"\n]*)\"" _ "${text}")

  if(CMAKE_MATCH_2 STREQUAL "Drums")
    set(THINK_PLUGIN_CLAP_KIND "\"drum\"")
    set(THINK_PLUGIN_VST3_KIND "Drum")
  else()
    set(THINK_PLUGIN_CLAP_KIND "\"synthesizer\"")
    set(THINK_PLUGIN_VST3_KIND "Synth")
  endif()

  think_plugin_unique_id("${id}" uid)

  get_property(taken GLOBAL PROPERTY THINK_PLUGIN_UNIQUE_IDS)

  if(uid IN_LIST taken)
    message(FATAL_ERROR "plugin: ${id}'s unique id ${uid} is another "
                        "plugin's; add it to think_plugin_unique_id by hand")
  endif()

  set_property(GLOBAL APPEND PROPERTY THINK_PLUGIN_UNIQUE_IDS "${uid}")

  set(THINK_PLUGIN_DSP "${dsp}")
  set(THINK_PLUGIN_ID "${id}")
  set(THINK_PLUGIN_NAME "thinksynth ${name}")
  set(THINK_PLUGIN_UNIQUE_ID "${uid}")
  string(REGEX REPLACE "(.)(.)(.)(.)" "'\\1', '\\2', '\\3', '\\4'"
         THINK_PLUGIN_UNIQUE_ID_CHARS "${uid}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/DistrhoPluginInfo.h.in"
                 "${gen}/DistrhoPluginInfo.h" @ONLY)

  # What it compiles in, from the engine: see controlsgen.cpp.
  set(generated
      "${gen}/thinksynth_dsp.h"
      "${gen}/thinksynth_controls.h"
      "${gen}/thinksynth_ui_size.h"
      "${gen}/thinksynth_samples.cpp")

  file(GLOB samples "${dir}/samples/*.wav"
                    "${PROJECT_SOURCE_DIR}/dsp/samples/*.wav")

  add_custom_command(
      OUTPUT ${generated}
      COMMAND controlsgen "${dsp}" "${id}" "${gen}"
              "${dir}/samples" "${PROJECT_SOURCE_DIR}/dsp/samples"
      DEPENDS controlsgen "${dsp}" ${samples}
      COMMENT "Reading ${id}'s controls and samples")

  add_custom_target(${target}-generated DEPENDS ${generated})

  dpf_add_plugin(${target}
      TARGETS clap vst3 lv2
      UI_TYPE cairo
      FILES_COMMON Controls.cpp
      FILES_DSP ThinkPlugin.cpp "${gen}/thinksynth_samples.cpp"
      FILES_UI Panel.cpp ThinkUI.cpp)

  # The generated directory first, so its DistrhoPluginInfo.h is the one.
  target_include_directories(${target} BEFORE PUBLIC "${gen}")
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${target}-dsp PUBLIC think_embedded)

  foreach(t ${target} ${target}-dsp ${target}-ui ${target}-clap
            ${target}-vst3 ${target}-lv2 ${target}-lv2-ui)
    if(TARGET ${t})
      add_dependencies(${t} ${target}-generated)
    endif()
  endforeach()

  foreach(t ${target}-clap ${target}-vst3 ${target}-lv2 ${target}-lv2-ui)
    if(TARGET ${t})
      set_target_properties(${t} PROPERTIES CXX_STANDARD 11)
    endif()
  endforeach()

  set_property(GLOBAL APPEND PROPERTY THINK_PLUGIN_BUILT "${id}|${dsp}")
endfunction()

# The list, to paths.
if(THINK_PLUGIN_DSPS STREQUAL "all")
  file(GLOB dsps "${PROJECT_SOURCE_DIR}/dsp/*.dsp")
  list(SORT dsps)
else()
  set(dsps "")

  foreach(d IN LISTS THINK_PLUGIN_DSPS)
    if(EXISTS "${d}")
      get_filename_component(d "${d}" ABSOLUTE)
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/dsp/${d}.dsp")
      set(d "${PROJECT_SOURCE_DIR}/dsp/${d}.dsp")
    else()
      message(FATAL_ERROR "plugin: no such .dsp: ${d}")
    endif()

    list(APPEND dsps "${d}")
  endforeach()
endif()

foreach(d IN LISTS dsps)
  think_add_dsp_plugin("${d}")
endforeach()

# DPF makes these two with the first plugin, so they are here and not above.
# Its LV2 generator, which runs the plugin at build time to write its
# .ttl, casts away a const GCC warns about.
if(TARGET lv2_ttl_generator)
  target_compile_options(lv2_ttl_generator PRIVATE -Wno-discarded-qualifiers)
endif()

# And its window library as DPF builds it on its own, in C++11: its pugl
# wrapper includes <cstdbool>, and under this tree's C++17 that warns.
# Each plugin's entry points are too, in think_add_dsp_plugin. Its file
# browser, which no plugin here opens and pugl.cpp compiles in, without the
# one warning GCC has about its fixed-size path buffers.
if(TARGET dgl-cairo)
  set_target_properties(dgl-cairo PROPERTIES CXX_STANDARD 11)
  target_compile_options(dgl-cairo PRIVATE -Wno-format-overflow)
endif()

get_property(built GLOBAL PROPERTY THINK_PLUGIN_BUILT)
list(LENGTH built count)
message(STATUS "plugin: ${count} graph(s) as plugins")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# The editor, which is the same code whatever the graph, is tested on
# juno's, when juno is built: see uicheck.cpp and guicheck.cpp.
set(juno_gen "${CMAKE_CURRENT_BINARY_DIR}/juno")

if(TARGET thinksynth-juno)
  # The editor with no window. It writes what it drew to editor.png beside
  # itself.
  add_executable(uicheck uicheck.cpp Controls.cpp Panel.cpp)
  target_include_directories(uicheck PRIVATE
      "${juno_gen}" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(uicheck PRIVATE PkgConfig::CAIRO)
  add_dependencies(uicheck thinksynth-juno-generated)

  add_test(NAME uicheck
           COMMAND uicheck "${CMAKE_CURRENT_BINARY_DIR}/editor.png")

  # The editor through a host. X11 and its XTest extension (libxtst-dev),
  # so Linux, and only where XTest is there.
  find_package(X11)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT X11_Xtst_FOUND)
    message(STATUS "plugin: no XTest (libxtst-dev), so no guicheck")
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_Xtst_FOUND)
    add_executable(guicheck guicheck.cpp Controls.cpp Panel.cpp)
    target_include_directories(guicheck PRIVATE
        "${juno_gen}" "${CMAKE_CURRENT_SOURCE_DIR}"
        "${dpf_SOURCE_DIR}/distrho/src")
    target_link_libraries(guicheck PRIVATE X11::X11 X11::Xtst PkgConfig::CAIRO
                                           ${CMAKE_DL_LIBS})
    add_dependencies(guicheck thinksynth-juno-clap thinksynth-juno-generated)

    add_test(NAME guicheck
             COMMAND guicheck "${PROJECT_BINARY_DIR}/bin/thinksynth-juno.clap")
    set_tests_properties(guicheck PROPERTIES
                         SKIP_REGULAR_EXPRESSION "SKIP  no display")
  endif()
endif()

# Each built CLAP against the engine playing its .dsp: see plugincheck.cpp.
# dlopen, so Unix. The engine side finds the graph's samples on disk, and
# the plugin its own compiled-in copies, so a sample embedded wrong is a
# difference.
if(NOT WIN32)
  add_executable(plugincheck plugincheck.cpp ReadControls.cpp)
  target_include_directories(plugincheck PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${dpf_SOURCE_DIR}/distrho/src")
  target_link_libraries(plugincheck PRIVATE think_embedded ${CMAKE_DL_LIBS})

  foreach(b IN LISTS built)
    string(REPLACE "|" ";" b "${b}")
    list(GET b 0 id)
    list(GET b 1 dsp)
    get_filename_component(dir "${dsp}" DIRECTORY)

    add_dependencies(plugincheck thinksynth-${id}-clap)
    add_test(NAME plugincheck.${id}
             COMMAND plugincheck
                     "${PROJECT_BINARY_DIR}/bin/thinksynth-${id}.clap"
                     "${dsp}" "${id}")
    set_tests_properties(plugincheck.${id} PROPERTIES
        ENVIRONMENT "THINK_DSP_PATH=${dir}")
  endforeach()
endif()

# The formats' own validators, run on what was built: clap-validator on
# each CLAP, pluginval on each VST3 at its strictest, its editor tests
# included -- it opens the editor and automates it. Linux builds of both,
# fetched at pinned releases, so Linux only. CMAKE_TEST_LAUNCHER --
# scripts/headless.sh -- reaches only a test whose command is a target this
# build makes, and these two are fetched binaries named by path, so it
# goes on them by hand: it is what keeps an editor on a private display.
# The one warning clap-validator gives -- state that loads from random
# bytes -- is DPF's state reader, not this plugin.
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

  foreach(b IN LISTS built)
    string(REPLACE "|" ";" b "${b}")
    list(GET b 0 id)

    add_test(NAME plugin.clap-validator.${id}
             COMMAND ${CMAKE_TEST_LAUNCHER} "${clapvalidator_bin}" validate
                     "${PROJECT_BINARY_DIR}/bin/thinksynth-${id}.clap")

    add_test(NAME plugin.pluginval.${id}
             COMMAND ${CMAKE_TEST_LAUNCHER} "${pluginval_SOURCE_DIR}/pluginval"
                     --strictness-level 10
                     --validate-in-process
                     --validate "${PROJECT_BINARY_DIR}/bin/thinksynth-${id}.vst3")

    set_tests_properties(plugin.pluginval.${id} PROPERTIES TIMEOUT 900)
  endforeach()
endif()
