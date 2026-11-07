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
#           -DREFERENCE= -DLOCAL= -DKEY= -DUPDATE=
#
# HARNESS_B and EXTRA_ARGS_B make it a comparison: a second harness, or the
# same one with more arguments, run over the same files. Both sides have to
# succeed and print exactly the same thing; the lines that differ are the
# failure.
#
# REFERENCE makes it a comparison with a committed file instead, which is
# what notices a change to libthink itself; UPDATE writes the harness's
# output over that file, or over LOCAL for a toolchain KEY it is not for.

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

if(HARNESS_B OR EXTRA_ARGS_B OR REFERENCE)
  if(NOT HARNESS_B)
    set(HARNESS_B "${HARNESS}")
  endif()

  separate_arguments(extra_B NATIVE_COMMAND "${EXTRA_ARGS_B}")

  foreach(side A B)
    if(side STREQUAL "B" AND REFERENCE)
      break()
    elseif(side STREQUAL "A")
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

  if(REFERENCE)
    # Paths under CORPUS, so that one reference holds for any checkout.
    string(REPLACE "${CORPUS}/" "" out_A "${out_A}")
    set(_head "toolchain: ${KEY}\n")
    set(out_A "${_head}${out_A}")

    # Bit for bit, a render belongs to the compiler and its libm as much as
    # to the code, so a reference is held to the toolchain on its first
    # line. Another toolchain's is LOCAL, in the build tree, where no commit
    # carries it; with neither this skips, unless THINK_CORPUSHASH_STRICT
    # makes it fail and print what it rendered.
    set(held "${LOCAL}")
    set(out_B "")

    foreach(_f "${REFERENCE}" "${LOCAL}")
      if(EXISTS "${_f}")
        file(READ "${_f}" _text)
        string(FIND "${_text}" "${_head}" at)

        if(at EQUAL 0)
          set(held "${_f}")
          set(out_B "${_text}")
          break()
        endif()
      endif()
    endforeach()

    if(NOT EXISTS "${REFERENCE}")
      set(held "${REFERENCE}")
    endif()

    if(UPDATE)
      file(WRITE "${held}" "${out_A}")
      message(STATUS "wrote ${held}")
      return()
    endif()

    if(out_B STREQUAL "" AND "$ENV{THINK_CORPUSHASH_STRICT}" STREQUAL "")
      message("SKIP  no reference for ${KEY}")
      return()
    endif()
  endif()

  if(NOT out_A STREQUAL out_B)
    # Line by line with string(FIND) rather than as CMake lists: a list
    # splits on every `;' in a line and treats an unbalanced `[' as the
    # start of a bracket, and the two sides may not print the same number
    # of lines. A side that has run out shows as (nothing).
    set(differ 0)

    while(NOT out_A STREQUAL "" OR NOT out_B STREQUAL "")
      foreach(side A B)
        string(FIND "${out_${side}}" "\n" at)

        if(at EQUAL -1)
          set(line_${side} "${out_${side}}")
          set(out_${side} "")
        else()
          string(SUBSTRING "${out_${side}}" 0 ${at} line_${side})
          math(EXPR at "${at} + 1")
          string(SUBSTRING "${out_${side}}" ${at} -1 out_${side})
        endif()
      endforeach()

      if(NOT line_A STREQUAL line_B)
        foreach(side A B)
          if(line_${side} STREQUAL "")
            set(line_${side} "(nothing)")
          endif()
        endforeach()

        message("A  ${line_A}\nB  ${line_B}")
        math(EXPR differ "${differ} + 1")
      endif()
    endwhile()

    if(REFERENCE)
      get_filename_component(_name "${REFERENCE}" NAME_WE)
      message(FATAL_ERROR "${differ} line(s) differ from ${held} over "
                          "${count} ${MODE} files. If the change is meant, "
                          "the ${_name}-update target rewrites it.")
    endif()

    message(FATAL_ERROR "${differ} line(s) differ between ${HARNESS} and "
                        "${HARNESS_B} ${EXTRA_ARGS_B} over ${count} ${MODE} "
                        "files")
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
