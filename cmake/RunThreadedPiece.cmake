# genwav over a piece twice, run via `cmake -P': on the audio thread alone
# and with its channels on THREADS more, and the two WAVs held to be the same
# bytes. thSynth sums the channels in one order whoever rendered them, and
# puts channels sharing a plugin's synth-wide table on one thread; a plugin
# that keeps state across channels and is not on that list is what this
# catches, as a render that moves with the threads.
#
# Required: -DGENWAV= -DPIECE= -DPLUGIN_DIR= -DDSP_PATH= -DOUT= -DTHREADS=
#           -DSECONDS=

foreach(var GENWAV PIECE PLUGIN_DIR DSP_PATH OUT THREADS SECONDS)
  if(NOT ${var})
    message(FATAL_ERROR "RunThreadedPiece.cmake: ${var} is required")
  endif()
endforeach()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

foreach(threads 0 ${THREADS})
  execute_process(
      COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s ${SECONDS} -q -j ${threads}
              -o "${OUT}-${threads}.wav" "${PIECE}"
      RESULT_VARIABLE rc
      ERROR_VARIABLE err)

  # 3 is a piece that clips, which is the piece's business and not this.
  if(NOT (rc EQUAL 0 OR rc EQUAL 3))
    message(FATAL_ERROR "genwav -j ${threads} exited ${rc}: ${err}")
  endif()

  file(SHA256 "${OUT}-${threads}.wav" sum_${threads})
endforeach()

if(NOT sum_0 STREQUAL sum_${THREADS})
  message(FATAL_ERROR
      "${PIECE} renders differently on ${THREADS} more threads than on one")
endif()
