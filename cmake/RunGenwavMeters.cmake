if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH OR NOT TAPE)
  message(FATAL_ERROR "GENWAV, PIECE, PLUGIN_DIR, DSP_PATH and TAPE are required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 3 --levels --sections
            -q -t "${TAPE}" "${PIECE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE report
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "genwav exited ${rc}: ${report}")
endif()

file(READ "${TAPE}" tape)

if(NOT tape MATCHES "^# channel 0 = plink\nN ")
  message(FATAL_ERROR "the tape did not name the instrument before its events")
endif()

if(NOT report MATCHES "1  plink[ ]+([0-9]+\\.[0-9]+)  ([0-9]+\\.[0-9]+)\n")
  message(FATAL_ERROR "the channel's peak and RMS row is missing: ${report}")
endif()
set(channel_peak "${CMAKE_MATCH_1}")
set(channel_rms "${CMAKE_MATCH_2}")

if(NOT channel_peak GREATER 0 OR NOT channel_rms GREATER 0)
  message(FATAL_ERROR "the instrument's measured level is silent: ${report}")
endif()

if(NOT report MATCHES "quiet[ ]+([0-9]+\\.[0-9]+)\n")
  message(FATAL_ERROR "the quiet section has no RMS row: ${report}")
endif()
set(quiet_rms "${CMAKE_MATCH_1}")

if(NOT report MATCHES "loud[ ]+([0-9]+\\.[0-9]+)\n")
  message(FATAL_ERROR "the loud section has no RMS row: ${report}")
endif()
set(loud_rms "${CMAKE_MATCH_1}")

# The window crossing the boundary can contain the first loud note and
# contribute a little to the preceding section.
if(NOT loud_rms GREATER quiet_rms OR quiet_rms GREATER 0.01)
  message(FATAL_ERROR "the section levels did not follow the arrangement: ${report}")
endif()

file(REMOVE "${TAPE}")
