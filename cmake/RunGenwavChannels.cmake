# scripts/guard/channels.gen through `genwav --levels', run via `cmake -P':
# twenty instruments, more than MIDI's sixteen channels, each on an engine
# channel of its own and each heard.
#
# Required: -DGENWAV= -DPIECE= -DPLUGIN_DIR= -DDSP_PATH=

if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH)
  message(FATAL_ERROR "GENWAV, PIECE, PLUGIN_DIR and DSP_PATH are required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 4 --levels -q "${PIECE}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE report
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "genwav exited ${rc}: ${out}${report}")
endif()

# A row each: the file's channel, the engine's, the instrument, and a peak
# above silence.
foreach(channel RANGE 1 20)
  math(EXPR engine "${channel} - 1")

  if(channel LESS 10)
    set(name "i0${channel}")
  else()
    set(name "i${channel}")
  endif()

  if(NOT "${out}${report}" MATCHES
     "\n[ ]+${channel}[ ]+${engine}  ${name}[ ]+([0-9]+\\.[0-9]+)  ")
    message(FATAL_ERROR "no level row for ${name} on channel ${channel}: "
                        "${out}${report}")
  endif()

  if(NOT CMAKE_MATCH_1 GREATER 0)
    message(FATAL_ERROR "${name} on channel ${channel} is silent: "
                        "${out}${report}")
  endif()
endforeach()
