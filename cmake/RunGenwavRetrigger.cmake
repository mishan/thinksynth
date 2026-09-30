# scripts/guard/retrigger.gen through genwav: a key struck again while it
# sounds, and a key struck twice at once, each hold for as long as the
# piece says and no longer. A pending off ending the wrong voice reads as
# a silent section; a voice nothing ends reads as a loud `*_ended' one.
#
# Three times: on organ0 as it is, and on copies made mono and choke,
# the two channel modes that key voices their own way (a mono channel
# keeps a stack of held pitches; a choke channel releases every voice
# when any note lands).
if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH OR NOT WORK)
  message(FATAL_ERROR "GENWAV, PIECE, PLUGIN_DIR, DSP_PATH and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

file(READ "${DSP_PATH}/organ0.dsp" organ)
file(READ "${PIECE}" piece)

string(FIND "${organ}" "node ionode {" at)
if(at LESS 0)
  message(FATAL_ERROR "organ0.dsp has no `node ionode {' to add a mode to")
endif()

file(WRITE "${WORK}/organ0.dsp" "${organ}")

foreach(mode mono choke)
  string(REPLACE "node ionode {" "node ionode {\n    ${mode} = 1;"
         variant "${organ}")
  file(WRITE "${WORK}/organ${mode}.dsp" "${variant}")
endforeach()

set(ENV{THINK_DSP_PATH} "${WORK}")

foreach(dsp organ0 organmono organchoke)
  string(REPLACE "dsp \"organ0.dsp\"" "dsp \"${dsp}.dsp\"" text "${piece}")
  file(WRITE "${WORK}/${dsp}.gen" "${text}")

  execute_process(
      COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 16 --sections -q
              "${WORK}/${dsp}.gen"
      OUTPUT_VARIABLE out
      ERROR_VARIABLE report
      RESULT_VARIABLE rc)

  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "genwav exited ${rc} on ${dsp}: ${report}")
  endif()

  # Held, the organ is at about 0.034 RMS; released, it is gone within a
  # few milliseconds.
  foreach(section retrigger doubled reversed)
    if(NOT report MATCHES "\n${section}[ ]+([0-9]+\\.[0-9]+)\n")
      message(FATAL_ERROR "${dsp}: no RMS row for ${section}: ${report}")
    endif()

    if(CMAKE_MATCH_1 LESS 0.02)
      message(FATAL_ERROR
          "${dsp}: section ${section} is silent (${CMAKE_MATCH_1}): a "
          "pending off ended the voice that replaced its note\n${report}")
    endif()

    if(NOT report MATCHES "\n${section}_ended[ ]+([0-9]+\\.[0-9]+)\n")
      message(FATAL_ERROR "${dsp}: no RMS row for ${section}_ended: ${report}")
    endif()

    if(CMAKE_MATCH_1 GREATER 0.001)
      message(FATAL_ERROR
          "${dsp}: section ${section}_ended still sounds (${CMAKE_MATCH_1}): "
          "a voice outlived every off on its key\n${report}")
    endif()
  endforeach()
endforeach()

file(REMOVE_RECURSE "${WORK}")
