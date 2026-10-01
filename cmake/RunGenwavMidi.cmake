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
    COMMAND "${GENWAV}" -p "${PLUGIN_DIR}" -s 4.9 -q --midi "${OUT}" "${PIECE}"
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
# sections, round again, in 4.9 seconds.
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

# And the same piece through thcMidiExport::render -- the Composer's and
# the page's Export MIDI -- which steps the piece on a silent synth of its
# own: the same file. At 4.9 s, which no event lands on: genwav steps in
# audio windows and stops a little past the time asked for, render stops
# on it, and a note struck at the stop is one of no length.
execute_process(
    COMMAND "${MIDICHECK}" --export "${PLUGIN_DIR}" 4.9 "${PIECE}" "${OUT}.export"
    ERROR_VARIABLE report
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "midicheck --export exited ${rc}: ${report}")
endif()

execute_process(
    COMMAND "${MIDICHECK}" --read "${OUT}.export"
    OUTPUT_VARIABLE exported
    RESULT_VARIABLE rc)

file(REMOVE "${OUT}.export")

if(NOT rc EQUAL 0 OR NOT exported STREQUAL dump)
  message(FATAL_ERROR
      "Export MIDI wrote a different file from genwav --midi:\n"
      "genwav:\n${dump}\nexport:\n${exported}")
endif()

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
