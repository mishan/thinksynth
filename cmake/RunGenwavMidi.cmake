# scripts/guard/midi.gen through `genwav --midi', read back by
# `midicheck --read': what genwav hands thcMidiFile, which midicheck's own
# cases, built from hand-made events, cannot see -- the chains' names, the
# channel's chanarg range, and the section markers, round the arrangement
# again where it has no end.
if(NOT GENWAV OR NOT MIDICHECK OR NOT PIECE OR NOT PLUGIN_DIR OR NOT DSP_PATH
   OR NOT OUT)
  message(FATAL_ERROR
      "GENWAV, MIDICHECK, PIECE, PLUGIN_DIR, DSP_PATH and OUT are required")
endif()

set(ENV{THINK_DSP_PATH} "${DSP_PATH}")
file(REMOVE "${OUT}")

execute_process(
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 5 -q --midi "${OUT}" "${PIECE}"
    ERROR_VARIABLE report
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "genwav exited ${rc}: ${report}")
endif()

execute_process(
    COMMAND "${MIDICHECK}" --read "${OUT}"
    OUTPUT_VARIABLE dump
    RESULT_VARIABLE rc)

file(REMOVE "${OUT}")

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the file genwav wrote does not parse: ${dump}")
endif()

# At 120 bpm and 480 to the beat a second is 960 ticks: two one-second
# sections, round again, in five seconds.
set(expect
    "format 1 division 480 tracks 3"
    "track 0 name 0 midi"
    "track 0 marker 0 a"
    "track 0 marker 960 b"
    "track 0 marker 1920 a"
    "track 0 marker 2880 b"
    "track 0 marker 3840 a\ntrack 0 notes"
    "track 1 name 0 low"
    "track 1 text 0 CC 20 = amp ("
    "track 2 name 0 high"
    "track 2 text 0 CC 20 = amp (")

foreach(line IN LISTS expect)
  string(FIND "${dump}" "${line}" at)

  if(at LESS 0)
    message(FATAL_ERROR "expected `${line}' in:\n${dump}")
  endif()
endforeach()

foreach(track 1 2)
  if(NOT dump MATCHES "track ${track} notes ([0-9]+) controls ([0-9]+)")
    message(FATAL_ERROR "no counts for track ${track}:\n${dump}")
  endif()

  if(CMAKE_MATCH_1 LESS 9 OR CMAKE_MATCH_2 LESS 1)
    message(FATAL_ERROR
        "track ${track} has ${CMAKE_MATCH_1} notes and ${CMAKE_MATCH_2} "
        "controller changes:\n${dump}")
  endif()
endforeach()
