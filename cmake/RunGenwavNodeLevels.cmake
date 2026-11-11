# genwav --node-levels over scripts/guard/nodelevels.gen, run via `cmake -P'.
#
# Four claims:
#   - the flag changes nothing in the render: it is bitwise the render
#     --interpret gives, which is what the flag runs the graph as;
#   - each node is measured in the loudest voice, not summed across
#     voices: vca reads -3.1 dBFS on a two-note chord;
#   - a node silent in every voice says so, one that went non-finite says
#     that, one the engine never ran says that instead, and no other node
#     is called any of them; an output in Hz is given in Hz;
#   - a graph the build compiled is measured too, run on its nodes for it.
#
# Required: -DGENWAV= -DPIECE= -DPLUGIN_DIR= -DDSP_PATH= -DWORK=

if(NOT GENWAV OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH OR NOT WORK)
  message(FATAL_ERROR "RunGenwavNodeLevels.cmake: GENWAV, PIECE, PLUGIN_DIR, DSP_PATH and WORK are all required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 2 --node-levels
            -o "${WORK}/levels.wav" "${PIECE}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
set(said "${out}${err}")
message(STATUS "${said}")

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "genwav --node-levels exited ${rc}")
endif()

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 2 --interpret -q
            -o "${WORK}/interpret.wav" "${PIECE}"
    RESULT_VARIABLE rc)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK}/levels.wav"
            "${WORK}/interpret.wav"
    RESULT_VARIABLE differ)

if(NOT rc EQUAL 0 OR differ)
  message(FATAL_ERROR "--node-levels changed the render: it differs from --interpret's")
endif()

# One line a node output: name, peak / RMS, then a flag or nothing. The
# bell's carrier last: left compiled, no node of the bell runs and every
# one reads silent.
foreach(want
    "\n  vca:out +-3\\.[01] / [-0-9.]+\n"
    "\n  osc:out +-3\\.[01] / [-0-9.]+\n"
    "\n  muted:out +-inf / -inf +silent in 2 of 2 voices\n"
    "\n  lfo:out +not run\n"
    "\n  nan:out +-inf / -inf +non-finite in 2 of 2 voices\n"
    "\n  freq:out +392\\.0 / 392\\.0 Hz\n"
    "\n  carrier:out +-?[0-9]+\\.[0-9] / -?[0-9]+\\.[0-9]\n")
  if(NOT said MATCHES "${want}")
    message(FATAL_ERROR "genwav --node-levels never said: ${want}")
  endif()
endforeach()

string(REGEX MATCHALL "non-finite in" bad "${said}")
list(LENGTH bad bad)

if(NOT bad EQUAL 1)
  message(FATAL_ERROR "${bad} outputs called non-finite; 1 was expected")
endif()

string(REGEX MATCHALL "silent in" silent "${said}")
string(REGEX MATCHALL "not run" unrun "${said}")
list(LENGTH silent silent)
list(LENGTH unrun unrun)

# lfo has two outputs, out and sync.
if(NOT silent EQUAL 1 OR NOT unrun EQUAL 2)
  message(FATAL_ERROR "${silent} outputs called silent and ${unrun} not run; 1 and 2 were expected")
endif()
