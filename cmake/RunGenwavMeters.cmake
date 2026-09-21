if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH OR NOT TAPE)
  message(FATAL_ERROR "GENWAV, PIECE, PLUGIN_DIR, DSP_PATH and TAPE are required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

# The whole arrangement, and then a render cut off inside the first
# section. Both go through here so the tape and the tables are read the
# same way; `report' is whichever run is being looked at.
function(render seconds tape_out report_out)
  execute_process(
      COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s "${seconds}" --levels
              --sections -q -t "${TAPE}" "${PIECE}"
      OUTPUT_VARIABLE out
      ERROR_VARIABLE report
      RESULT_VARIABLE rc)

  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "genwav exited ${rc}: ${report}")
  endif()

  file(READ "${TAPE}" tape)

  # Before anything can fail, so a failing run does not leave it behind.
  file(REMOVE "${TAPE}")

  set(${tape_out} "${tape}" PARENT_SCOPE)
  set(${report_out} "${report}" PARENT_SCOPE)
endfunction()

render(3 tape report)

if(NOT tape MATCHES "^# channel 0 = plink\nN ")
  message(FATAL_ERROR "the tape did not name the instrument before its events")
endif()

if(NOT report MATCHES "1[ ]+0  plink[ ]+([0-9]+\\.[0-9]+)  ([0-9]+\\.[0-9]+)\n")
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

# One row per section, in the order the file declares them.
string(FIND "${report}" "quiet" quiet_at)
string(FIND "${report}" "loud" loud_at)

if(quiet_at LESS 0 OR NOT loud_at GREATER quiet_at)
  message(FATAL_ERROR "the sections are not in declaration order: ${report}")
endif()

# Cut off inside the first section: the second has not played, and a
# section that has not played reads zero rather than being left out.
render(0.5 short_tape short_report)

if(NOT short_report MATCHES "loud[ ]+([0-9]+\\.[0-9]+)\n")
  message(FATAL_ERROR "a section that has not played lost its row: ${short_report}")
endif()

if(NOT CMAKE_MATCH_1 EQUAL 0)
  message(FATAL_ERROR "a section that has not played did not read zero: ${short_report}")
endif()
