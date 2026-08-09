# Driver for the corpus-wide tests, run via `cmake -P'.
#
# The file lists cannot be baked in at configure time: which DSPs are eligible
# depends on what each file *references*, and filtering by name would go stale
# the moment a DSP is fixed or added. Doing it in CMake script mode rather
# than in shell keeps it working on Windows, where these same tests have to
# run under MSYS2 but should not depend on a POSIX shell being the one that
# invokes ctest.
#
# Required: -DHARNESS= -DCORPUS= -DPLUGIN_DIR= -DMODE=dsp|patch
# Optional: -DEXTRA_ARGS=

if(NOT HARNESS OR NOT CORPUS OR NOT PLUGIN_DIR OR NOT MODE)
  message(FATAL_ERROR "RunHarness.cmake: HARNESS, CORPUS, PLUGIN_DIR and MODE are all required")
endif()

if(MODE STREQUAL "dsp")
  file(GLOB_RECURSE candidates "${CORPUS}/*.dsp")

  # Plugins that compile but are deliberately not in the built set: input/wav,
  # input/alsa, misc/wlan. Eleven of the 92 shipped DSPs reference one of
  # them and cannot load. Excluded by what they reference, so the list cannot
  # drift -- build the plugin and its DSPs join the sweep by themselves.
  set(exclude_re "(input::|misc::wlan|fft::|test::)")

  set(files "")
  foreach(f IN LISTS candidates)
    file(READ "${f}" contents)
    if(NOT contents MATCHES "${exclude_re}")
      list(APPEND files "${f}")
    endif()
  endforeach()

elseif(MODE STREQUAL "patch")
  file(GLOB_RECURSE candidates "${CORPUS}/*.patch")

  # Rythmic.patch and Rythmic-2.patch point at an absolute
  # /usr/local/share//thinksynth/dsp/mfm03.dsp that is not in the tree --
  # leftovers from the "don't use absolute paths for patch files" cleanup.
  # Remove this filter once they are repointed.
  set(files "")
  foreach(f IN LISTS candidates)
    if(NOT f MATCHES "Rythmic")
      list(APPEND files "${f}")
    endif()
  endforeach()

else()
  message(FATAL_ERROR "RunHarness.cmake: unknown MODE '${MODE}'")
endif()

list(LENGTH files count)
if(count EQUAL 0)
  message(FATAL_ERROR "RunHarness.cmake: no ${MODE} files found under ${CORPUS}")
endif()

list(SORT files)

separate_arguments(extra NATIVE_COMMAND "${EXTRA_ARGS}")

message(STATUS "${MODE}: ${count} files")

execute_process(
    COMMAND "${HARNESS}" ${extra} -p "${PLUGIN_DIR}" ${files}
    RESULT_VARIABLE rc)

# Every harness returns the number of files that failed, so a non-zero status
# is both the failure signal and the count.
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${HARNESS}: ${rc} failure(s) over ${count} ${MODE} files")
endif()
