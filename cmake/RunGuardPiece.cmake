# genwav over the piece that goes wrong on purpose, run via `cmake -P'.
#
# scripts/guard/nonfinite.gen is one instrument that cannot help producing a
# NaN and one that behaves. Three claims about the render:
#
#   - it exits 4, the status for a render the guard fired during -- beside 3
#     for clipping and ahead of it, since a piece that clips is loud and a
#     piece with a non-finite voice in it is not the piece;
#   - the summary says how many voices that was, so the count is in the log
#     and not only in the status;
#   - it still has a peak: the good instrument goes on sounding.
#
# Required: -DGENWAV= -DPIECE= -DPLUGIN_DIR= -DDSP_PATH=

if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH)
  message(FATAL_ERROR "RunGuardPiece.cmake: GENWAV, PIECE, PLUGIN_DIR and DSP_PATH are all required")
endif()

# The piece names its instruments by bare filename, and ctest runs in the
# build tree where nothing relative resolves. Same belt as RunHarness.cmake.
set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 4 "${PIECE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)

set(said "${out}${err}")

message(STATUS "${said}")

if(NOT rc EQUAL 4)
  message(FATAL_ERROR
      "genwav exited ${rc} on ${PIECE}; 4 was expected -- the guard either "
      "did not fire or genwav is no longer counting what it caught")
endif()

if(NOT said MATCHES "non-finite voices: [1-9]")
  message(FATAL_ERROR
      "genwav exited 4 but its summary never said how many voices that was")
endif()

# "peak 0.000" is what the whole mix looked like when one bad voice could take
# the rest of it with it.
if(said MATCHES "peak 0\\.000")
  message(FATAL_ERROR
      "the render is silent: a non-finite voice took the mix with it")
endif()
