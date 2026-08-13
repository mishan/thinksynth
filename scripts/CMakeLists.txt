# The headless harnesses.
#
# These need no audio backend and no plugins beyond the ones in the build
# tree, which is what makes them usable as CI gates on all three platforms.
# Nine are registered with add_test below; the rest take a corpus argument or
# their own build tree and are run by hand.
#
# editorcheck is the one exception to "no display": it builds real widgets and
# skips itself loudly without one, which is why CI runs the Linux suite under
# xvfb-run.

# think_harness(<name> [EXTRA <source>...])
function(think_harness name)
  cmake_parse_arguments(H "NODEMODEL;THREADS;VISUALHOST" "" "EXTRA" ${ARGN})

  set(sources "${name}.cpp" ${H_EXTRA})

  if(H_NODEMODEL)
    list(APPEND sources $<TARGET_OBJECTS:think_nodemodel>)
  endif()

  if(H_VISUALHOST)
    list(APPEND sources $<TARGET_OBJECTS:think_visualhost>)
  endif()

  add_executable(${name} ${sources})

  target_include_directories(${name} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/src")

  target_link_libraries(${name} PRIVATE think)

  if(H_VISUALHOST)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/libthink")
    target_link_libraries(${name} PRIVATE PkgConfig::CAIRO)
  endif()

  if(H_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE Threads::Threads)
  endif()

  set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/scripts")

  # The harnesses live in their own directory and link libthink, so on Windows
  # each one needs its own copy of the DLL beside it for the same reason the
  # application does.
  think_copy_runtime_dlls(${name})
endfunction()

# gthPatchfile.cpp lives in the binary rather than in libthink, so dspcheck
# pulls it in directly to cover the .patch path. It needs sigc++ but not
# gtkmm.
# Data-file lookup: the answers have to stay true after the working directory
# moves. See the header of pathcheck.cpp for the bug that was not caught.
think_harness(pathcheck)

# The light/dark decision. Only the Linux half of the platform readers can be
# run here at all, so what gets pinned is the part that is the same on all
# three -- and that part lives in its own file precisely so this harness needs
# neither gtkmm nor, on macOS, CoreFoundation. Compiling all of gthTheme.cpp
# to reach it is what broke the macOS build.
think_harness(themecheck EXTRA "${PROJECT_SOURCE_DIR}/src/gthThemePolicy.cpp")

think_harness(dspcheck EXTRA "${PROJECT_SOURCE_DIR}/src/gthPatchfile.cpp")

think_harness(dsplevel)
think_harness(dspab)
# dspstress runs each level in a forked child with an alarm(2) watchdog, so
# that a level which segfaults or wedges does not take the run with it. That
# is fork(), waitpid() and SIGALRM -- none of which Windows has, and the
# obvious rewrite (threads, no isolation) would lose the property that makes
# the harness useful. It is also a ThreadSanitizer harness, and there is no
# TSan for MinGW. So it is simply not built there.
if(NOT WIN32)
  think_harness(dspstress THREADS)
endif()

# The audio path's block-size handling, without a sound card. gthSynthSource
# is deliberately free of both gtkmm and RtAudio so this can link it directly.
think_harness(dspblock EXTRA "${PROJECT_SOURCE_DIR}/src/gthSynthSource.cpp")

# The MIDI thread handoff, without a MIDI device. gthMidiQueue needs glibmm
# for Glib::Dispatcher but not gtkmm, so this stays headless.
think_harness(dspmidi THREADS EXTRA "${PROJECT_SOURCE_DIR}/src/gthMidiQueue.cpp")
target_link_libraries(dspmidi PRIVATE PkgConfig::GLIBMM)

# thSampleRing on its own, with no synth around it. Header-only, so this links
# libthink only for the include path.
think_harness(ringcheck THREADS)

# The visual plugins, drawn to a cairo image surface. No toolkit and no
# display, which is the practical reason the visual ABI takes a cairo_t rather
# than a widget -- it means these gate on all three CI runners.
think_harness(visualcheck VISUALHOST)
# plugins/visual for fftr.h: visualcheck checks the shared FFT numerically,
# which is the one place in the visualizer work where "are the numbers right"
# has an answer.
target_include_directories(visualcheck PRIVATE "${PROJECT_SOURCE_DIR}/plugins/visual")

# dspprobe uses NodeGraph to enumerate output ports, so that what it checks is
# what the editor would offer rather than a second opinion about which args are
# outputs.
think_harness(dspprobe  NODEMODEL)

# The one harness here that needs a display, because what it measures is a real
# widget's real draw path. A measuring instrument rather than a gate, so it is
# not in the ctest list below -- run it by hand, under xvfb-run if need be.
think_harness(canvasbench NODEMODEL VISUALHOST
              EXTRA "${PROJECT_SOURCE_DIR}/src/gui/NodeCanvas.cpp")
target_link_libraries(canvasbench PRIVATE PkgConfig::GTKMM)

# editorcheck joins up what the other harnesses cover separately: the tap, the
# module and the panel are three things, and NodeEditor is the only thing that
# holds them together. It needs a display for the same reason canvasbench does
# -- real widgets -- but unlike canvasbench it is a gate, so it is in the ctest
# list below, guarded by whether a display can be had.
think_harness(editorcheck NODEMODEL VISUALHOST
              EXTRA "${PROJECT_SOURCE_DIR}/src/gui/NodeCanvas.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeEditor.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodeParams.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/NodePalette.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/Dialogs.cpp"
                    "${PROJECT_SOURCE_DIR}/src/gui/SaveButton.cpp")
target_link_libraries(editorcheck PRIVATE PkgConfig::GTKMM)

think_harness(dspgraph  NODEMODEL)
think_harness(dsplayout NODEMODEL)
think_harness(dsplive   NODEMODEL)
think_harness(dspnew    NODEMODEL)
think_harness(dspwrite  NODEMODEL)

# ---------------------------------------------------------------------------
# CTest gates. See cmake/RunHarness.cmake for why the file lists are computed
# at test time rather than at configure time.
# ---------------------------------------------------------------------------

set(_runner "${PROJECT_SOURCE_DIR}/cmake/RunHarness.cmake")

add_test(NAME dspcheck.dsp
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspcheck>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

add_test(NAME dspcheck.patch
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspcheck>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/patches
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DWORKING_DIR=${PROJECT_SOURCE_DIR}/dsp
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=patch
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

# Exit status is the number of measurements still exceeding TH_MAX, which
# should be zero at every voice count.
add_test(NAME dsplevel
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dsplevel>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 "-DEXTRA_ARGS=-v 4"
                 -P "${_runner}")

# Rendering each patch eight times over is slow, so this runs on a
# representative handful rather than the whole corpus: what is being tested is
# gthSynthSource, not the DSPs.
add_test(NAME dspmidi COMMAND dspmidi 200000)

add_test(NAME dspblock
         COMMAND dspblock -q -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp/ts1.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/anasync.dsp"
                 "${PROJECT_SOURCE_DIR}/dsp/mfm01.dsp")

# Cheap and entirely self-contained, so it runs the full sweep rather than a
# representative sample.
add_test(NAME ringcheck COMMAND ringcheck 20000)

add_test(NAME pathcheck COMMAND pathcheck)

add_test(NAME themecheck COMMAND themecheck)

# Needs no corpus and no display -- it draws to an image surface.
add_test(NAME visualcheck
         COMMAND visualcheck -p "${PROJECT_BINARY_DIR}/plugins/")

add_test(NAME dspprobe
         COMMAND "${CMAKE_COMMAND}"
                 -DHARNESS=$<TARGET_FILE:dspprobe>
                 -DCORPUS=${PROJECT_SOURCE_DIR}/dsp
                 -DPLUGIN_DIR=${PROJECT_BINARY_DIR}/plugins/
                 -DDSP_PATH=${PROJECT_SOURCE_DIR}/dsp
                 -DMODE=dsp
                 -DEXTRA_ARGS=-q
                 -P "${_runner}")

# Skips itself, loudly, where there is no display -- see the comment in
# editorcheck.cpp. CI runs the whole suite under xvfb-run so that it does not.
add_test(NAME editorcheck
         COMMAND editorcheck -p "${PROJECT_BINARY_DIR}/plugins/"
                 "${PROJECT_SOURCE_DIR}/dsp/ts1.dsp")

set_tests_properties(dspcheck.dsp dspcheck.patch dsplevel dspblock dspmidi
                     ringcheck visualcheck dspprobe editorcheck
                     PROPERTIES TIMEOUT 900)
