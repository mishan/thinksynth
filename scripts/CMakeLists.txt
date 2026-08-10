# The headless harnesses.
#
# These need no audio backend, no display and no plugins beyond the ones in
# the build tree, which is what makes them usable as CI gates on all three
# platforms. scripts/Makefile stays for now as a standalone fallback; it goes
# when autotools does.

# think_harness(<name> [EXTRA <source>...])
function(think_harness name)
  cmake_parse_arguments(H "NODEMODEL;THREADS" "" "EXTRA" ${ARGN})

  set(sources "${name}.cpp" ${H_EXTRA})

  if(H_NODEMODEL)
    list(APPEND sources $<TARGET_OBJECTS:think_nodemodel>)
  endif()

  add_executable(${name} ${sources})

  target_include_directories(${name} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/src")

  target_link_libraries(${name} PRIVATE think)

  if(H_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE Threads::Threads)
  endif()

  set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/scripts")
endfunction()

# gthPatchfile.cpp lives in the binary rather than in libthink, so dspcheck
# pulls it in directly to cover the .patch path. It needs sigc++ but not
# gtkmm.
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

set_tests_properties(dspcheck.dsp dspcheck.patch dsplevel dspblock dspmidi
                     PROPERTIES TIMEOUT 900)
