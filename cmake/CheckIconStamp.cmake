# Is src/thinksynth.ico still the icon that data/org.thinksynth.thinksynth.svg
# describes?
#
# The Windows executable embeds a .ico, which Windows cannot substitute for an
# SVG, so that file is rendered from the SVG and committed -- committed so that
# building for Windows needs no rasteriser on the machine doing it.  The cost
# of committing it is that the two can drift, and the drift is silent: nothing
# about editing the SVG makes the stale .ico stop working.
#
# So the generator records the SVG's hash beside the icon, and this compares it
# against the SVG as it stands now.  It hashes the input rather than the output
# on purpose; see the note in scripts/make-windows-icon.py.
#
# Run by ctest on every platform, since it is a file comparison and needs
# neither Python nor a renderer.
#
#   cmake -DTHINK_SOURCE_DIR=<tree> -P cmake/CheckIconStamp.cmake

if(NOT DEFINED THINK_SOURCE_DIR)
  message(FATAL_ERROR "THINK_SOURCE_DIR is not set")
endif()

set(_svg "${THINK_SOURCE_DIR}/data/org.thinksynth.thinksynth.svg")
set(_stamp "${THINK_SOURCE_DIR}/src/thinksynth-icon.stamp")

foreach(_f "${_svg}" "${_stamp}")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "missing ${_f}")
  endif()
endforeach()

file(SHA256 "${_svg}" _actual)

# The stamp is mostly a comment explaining itself to whoever this check fails
# on; the hash is the one line that is bare hex.
file(STRINGS "${_stamp}" _lines)

set(_recorded "")

# Length is checked with string(LENGTH) rather than a bounded repetition,
# because CMake's regex engine has no {n} operator -- "[0-9a-f]{64}" there
# matches a literal brace, not a count.
foreach(_line IN LISTS _lines)
  string(STRIP "${_line}" _line)
  string(LENGTH "${_line}" _len)

  if(_len EQUAL 64 AND _line MATCHES "^[0-9a-f]+$")
    set(_recorded "${_line}")
  endif()
endforeach()

if(_recorded STREQUAL "")
  message(FATAL_ERROR
    "no sha256 found in ${_stamp} -- regenerate it with\n"
    "    python3 scripts/make-windows-icon.py")
endif()

if(NOT _recorded STREQUAL _actual)
  message(FATAL_ERROR
    "src/thinksynth.ico is stale: it was rendered from a different version of\n"
    "data/org.thinksynth.thinksynth.svg than the one in the tree.\n"
    "\n"
    "  recorded: ${_recorded}\n"
    "  actual:   ${_actual}\n"
    "\n"
    "The .ico is committed rather than built, so editing the SVG does not\n"
    "update it. Re-render and commit both:\n"
    "\n"
    "    python3 scripts/make-windows-icon.py\n")
endif()

message(STATUS "thinksynth.ico is current with the SVG (${_actual})")
