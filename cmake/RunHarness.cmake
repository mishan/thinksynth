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
# Optional: -DEXTRA_ARGS= -DHARNESS_B= -DEXTRA_ARGS_B=
#
# HARNESS_B and EXTRA_ARGS_B make it a comparison: a second harness, or the
# same one with more arguments, run over the same files. Both sides have to
# succeed and print exactly the same thing; the lines that differ are the
# failure.

if(NOT HARNESS OR NOT CORPUS OR NOT PLUGIN_DIR OR NOT MODE)
  message(FATAL_ERROR "RunHarness.cmake: HARNESS, CORPUS, PLUGIN_DIR and MODE are all required")
endif()

if(MODE STREQUAL "dsp")
  file(GLOB_RECURSE candidates "${CORPUS}/*.dsp")

  # Plugins that compile but are deliberately not in the built set: input/wav,
  # input/alsa, misc/wlan. Two DSPs reference one of them and cannot load.
  # Excluded by what they reference, so the list cannot drift -- build the
  # plugin and its DSPs join the sweep by themselves.
  set(exclude_re "(input::|misc::wlan|fft::|test::)")

  set(files "")
  foreach(f IN LISTS candidates)
    file(READ "${f}" contents)

    # Comments first. `#' runs to the end of the line in this grammar, and a
    # file that *mentions* a plugin is not a file that references one:
    # dsp/fx/vocoder.dsp spends a paragraph on why it needs a side channel
    # rather than the input:: nodes the graphs before it read, and was
    # dropped from every corpus sweep for the whole time it said so. A
    # sixteen-band effect that nothing loaded is exactly what this filter
    # exists to prevent.
    string(REGEX REPLACE "#[^\n]*" "" contents "${contents}")

    if(NOT contents MATCHES "${exclude_re}")
      list(APPEND files "${f}")
    endif()
  endforeach()

elseif(MODE STREQUAL "patch")
  # No filter. Rythmic.patch and Rythmic-2.patch used to be excluded by name:
  # they pointed at an absolute /usr/local/share//thinksynth/dsp/mfm03.dsp
  # that was never in the tree. They now name mfm01.dsp, which declares
  # exactly the chanargs they set, so the whole corpus loads.
  file(GLOB_RECURSE files "${CORPUS}/*.patch")

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

# A .patch names its DSP by bare filename -- `dsp ts1.dsp' -- and ctest runs
# in the build tree, where nothing relative resolves. Two belts, because they
# cover different things:
#
#   WORKING_DIR      runs the sweep from the source tree's dsp/, which is all
#                    resolveDsp needs and works whatever it is looking in.
#   THINK_DSP_PATH   the explicit override thUtil::findDataFile reads, which
#                    also covers a DSP named from a subdirectory.
#
# This is why the patch gate passed locally and failed in CI for so long: a
# stale /usr/local/share/thinksynth/dsp on the development machine was
# quietly answering the compiled-in DSP_PATH fallback.
if(WORKING_DIR)
  set(_wd WORKING_DIRECTORY "${WORKING_DIR}")
endif()

if(DSP_PATH)
  set(ENV{THINK_DSP_PATH} "${DSP_PATH}")
endif()

if(HARNESS_B OR EXTRA_ARGS_B)
  if(NOT HARNESS_B)
    set(HARNESS_B "${HARNESS}")
  endif()

  separate_arguments(extra_B NATIVE_COMMAND "${EXTRA_ARGS_B}")

  foreach(side A B)
    if(side STREQUAL "A")
      set(_h "${HARNESS}")
      set(_x ${extra})
    else()
      set(_h "${HARNESS_B}")
      set(_x ${extra} ${extra_B})
    endif()

    execute_process(
        COMMAND "${_h}" ${_x} -p "${PLUGIN_DIR}" ${files}
        ${_wd}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out_${side})

    if(NOT rc EQUAL 0)
      message("${out_${side}}")
      message(FATAL_ERROR "${_h}: ${rc} failure(s) over ${count} ${MODE} files")
    endif()
  endforeach()

  if(NOT out_A STREQUAL out_B)
    string(REPLACE "\n" ";" lines_A "${out_A}")
    string(REPLACE "\n" ";" lines_B "${out_B}")
    list(LENGTH lines_A n)
    math(EXPR last "${n} - 1")

    set(differ 0)
    foreach(i RANGE ${last})
      list(GET lines_A ${i} a)
      list(GET lines_B ${i} b)
      if(NOT a STREQUAL b)
        message("A  ${a}\nB  ${b}")
        math(EXPR differ "${differ} + 1")
      endif()
    endforeach()

    message(FATAL_ERROR "${differ} of ${count} ${MODE} files differ between "
                        "${HARNESS} and ${HARNESS_B} ${EXTRA_ARGS_B}")
  endif()

  message(STATUS "${count} ${MODE} files identical")
  return()
endif()

execute_process(
    COMMAND "${HARNESS}" ${extra} -p "${PLUGIN_DIR}" ${files}
    ${_wd}
    RESULT_VARIABLE rc)

# Every harness returns the number of files that failed, so a non-zero status
# is both the failure signal and the count.
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${HARNESS}: ${rc} failure(s) over ${count} ${MODE} files")
endif()
