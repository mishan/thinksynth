# Are the committed icons still the icon that
# data/org.thinksynth.thinksynth.svg describes?
#
# Neither Windows nor macOS can use an SVG: the executable embeds a .ico and
# the bundle carries a .icns, and both are rendered from that SVG ahead of time
# and committed, so that building for either needs no rasteriser on the machine
# doing it.  The cost of committing them is that they can drift, and the drift
# is silent -- nothing about editing the SVG makes a stale render stop working.
#
# So the generator records the SVG's hash beside them, and this compares it
# against the SVG as it stands now.  It hashes the input rather than the
# outputs on purpose; see the note in scripts/make-icons.py.
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

# The renders themselves are checked only for existence. Whether they are the
# right *content* is what the hash below answers, indirectly; what this catches
# is one of them having been deleted or never committed, which would otherwise
# surface as a link error on Windows or a generic icon on macOS.
set(_ico "${THINK_SOURCE_DIR}/src/thinksynth.ico")
set(_icns "${THINK_SOURCE_DIR}/src/thinksynth.icns")

foreach(_f "${_svg}" "${_stamp}" "${_ico}" "${_icns}")
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
    "    python3 scripts/make-icons.py")
endif()

if(NOT _recorded STREQUAL _actual)
  message(FATAL_ERROR
    "The committed icons are stale: they were rendered from a different\n"
    "version of data/org.thinksynth.thinksynth.svg than the one in the tree.\n"
    "\n"
    "  recorded: ${_recorded}\n"
    "  actual:   ${_actual}\n"
    "\n"
    "src/thinksynth.ico and src/thinksynth.icns are committed rather than\n"
    "built, so editing the SVG does not update them. Re-render and commit\n"
    "all three together:\n"
    "\n"
    "    python3 scripts/make-icons.py\n")
endif()

message(STATUS "thinksynth.ico and thinksynth.icns are current with the SVG "
               "(${_actual})")
