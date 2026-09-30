# scripts/guard/retrigger.gen through genwav: a key struck again while it
# sounds, and a key struck twice at once, each hold for as long as the
# piece says. A pending off ending the wrong voice reads as a silent
# section.
if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH)
  message(FATAL_ERROR "GENWAV, PIECE, PLUGIN_DIR and DSP_PATH are required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 20 --sections -q "${PIECE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE report
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "genwav exited ${rc}: ${report}")
endif()

# Each section lies wholly inside the voice it is named for; the organ
# holds at about 0.034 RMS.
foreach(section retrigger doubled reversed)
  if(NOT report MATCHES "\n${section}[ ]+([0-9]+\\.[0-9]+)\n")
    message(FATAL_ERROR "no RMS row for section ${section}: ${report}")
  endif()

  if(CMAKE_MATCH_1 LESS 0.02)
    message(FATAL_ERROR
        "section ${section} is silent (${CMAKE_MATCH_1}): a pending off "
        "ended the voice that replaced its note\n${report}")
  endif()
endforeach()
