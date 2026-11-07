# What the application and its harnesses need beyond the engine: the toolkit,
# glibmm, cairomm, the audio and MIDI backends and the tiled panes. Included
# only with THINK_GUI; see the top-level CMakeLists.txt.

# A floor, so a toolkit that is too old says so once instead of failing in
# three hundred places. 4.10 is mullion-gtk's, which the main window's panes
# are; CI exercises 4.14, which is what Ubuntu 24.04 carries, and that is the
# version this is actually tested against.
pkg_check_modules(GTKMM  REQUIRED IMPORTED_TARGET gtkmm-4.0>=4.10)
pkg_check_modules(GTHREAD REQUIRED IMPORTED_TARGET gthread-2.0)
pkg_check_modules(GLIBMM REQUIRED IMPORTED_TARGET glibmm-2.68)

# cairomm for the two canvases' content classes, which draw through it and
# nothing of gtkmm's -- see src/CanvasContent.h. gtkmm brings it anyway; it
# is named here so those classes can be built with no gtkmm on the path.
pkg_check_modules(CAIROMM REQUIRED IMPORTED_TARGET cairomm-1.16)

# The one audio backend. RtAudio covers ALSA/JACK/Pulse/OSS, CoreAudio, and
# WASAPI/DirectSound/ASIO behind a single callback API.
#
# Deliberately not a requirement on the distribution's packaging. Making it
# one failed on all three CI platforms at once, and the reason is worse than a
# naming mismatch: Ubuntu 24.04 does not carry librtaudio-dev at all, and
# 22.04 carries RtAudio 5.2 -- while this code is written against 6, which
# reports errors by return code rather than by throwing and identifies devices
# by opaque id rather than by index. A system package would be missing on some
# runners and the wrong API on others.
#
# So the version floor is explicit and a source build is the normal case, not
# an emergency one. A system copy is used only when it is new enough.
include(FetchContent)

set(THINK_RTAUDIO_VERSION 6.0.1)
# We use the commit SHA in case the tag ever gets forced push maliciously
set(THINK_RTAUDIO_VERSION_TAG b4f04903312e0e0efffbe77655172e0f060dc085)

# Forces the fetch path even where pkg-config would have found something.
# Exists so the fallback can be exercised on a machine that has the system
# package -- an untested fallback is not a fallback.
option(THINK_FETCH_RT "Always build RtAudio and RtMidi from source" OFF)

if(NOT THINK_FETCH_RT)
  pkg_check_modules(RTAUDIO IMPORTED_TARGET "rtaudio>=6.0.0")
endif()

if(RTAUDIO_FOUND)
  set(THINK_RTAUDIO_LIB PkgConfig::RTAUDIO)
  set(THINK_RTAUDIO_ORIGIN "system ${RTAUDIO_VERSION}")
else()
  message(STATUS "rtaudio not found via pkg-config; fetching ${THINK_RTAUDIO_VERSION}")

  # Static, so a fetched build needs no runtime deployment story of its own.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)

  # Which APIs a fetched RtAudio builds, decided here rather than left to its
  # own probing.
  #
  # Left to itself it enables whatever it can find, and on a macOS runner with
  # Homebrew JACK installed that meant an interface carrying a bare `-ljack'
  # with no -L to go with it: `ld: library 'jack' not found', after everything
  # else had compiled. Being explicit also means the set of backends does not
  # depend on what happens to be installed on the machine that built it, which
  # for a shipped binary matters more than the convenience.
  #
  # JACK stays on for Linux, where it is the point. macOS gets CoreAudio and
  # Windows WASAPI; both can have JACK added deliberately by someone who wants
  # it and has the development files to hand.
  if(APPLE)
    set(RTAUDIO_API_CORE ON  CACHE BOOL "" FORCE)
    set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
  elseif(WIN32)
    set(RTAUDIO_API_WASAPI ON  CACHE BOOL "" FORCE)
    set(RTAUDIO_API_DS     OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ASIO   OFF CACHE BOOL "" FORCE)  # SDK is not redistributable
    set(RTAUDIO_API_JACK   OFF CACHE BOOL "" FORCE)
  else()
    # Linux, and said out loud rather than left to probing -- otherwise the
    # argument above ("a build's backends should not depend on what happened
    # to be installed") would hold everywhere except the platform this is
    # mostly developed on. ALSA is always there; JACK and PulseAudio are only
    # forced on if their development files are, so this pins the outcome
    # without demanding either be present.
    set(RTAUDIO_API_ALSA  ON CACHE BOOL "" FORCE)
    set(RTAUDIO_API_OSS   OFF CACHE BOOL "" FORCE)

    pkg_check_modules(THINK_PROBE_JACK QUIET jack)
    pkg_check_modules(THINK_PROBE_PULSE QUIET libpulse-simple)

    set(RTAUDIO_API_JACK  ${THINK_PROBE_JACK_FOUND}  CACHE BOOL "" FORCE)
    set(RTAUDIO_API_PULSE ${THINK_PROBE_PULSE_FOUND} CACHE BOOL "" FORCE)
  endif()

  # EXCLUDE_FROM_ALL keeps RtAudio's own install() rules out of ours. They are
  # not merely redundant for a statically linked dependency -- they are
  # broken: RtAudio writes RtAudioConfig.cmake into its own binary directory
  # and its install script looks for it in the top-level one, so `cmake
  # --install' and every cpack run fail with "cannot find
  # .../RtAudioConfig.cmake". CMake honours this for install rules from 3.28.
  #
  # They also both create a custom target called `uninstall', which cannot
  # share a name in one build; each exposes the name as a cache variable for
  # exactly that.
  set(RTAUDIO_TARGETNAME_UNINSTALL "rtaudio-uninstall" CACHE STRING "" FORCE)

  FetchContent_Declare(rtaudio
      GIT_REPOSITORY https://github.com/thestk/rtaudio.git
      GIT_TAG ${THINK_RTAUDIO_VERSION_TAG}
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL)

  if(CMAKE_VERSION VERSION_LESS 3.28)
    message(WARNING
        "CMake ${CMAKE_VERSION} cannot suppress a fetched dependency's "
        "install rules (3.28+ needed), so `cmake --install' and cpack will "
        "fail on RtAudio's. Install rtaudio >= 6 system-wide, or upgrade "
        "CMake, if you need to package this build.")
  endif()

  FetchContent_MakeAvailable(rtaudio)

  set(THINK_RTAUDIO_LIB rtaudio)
  set(THINK_RTAUDIO_ORIGIN "fetched ${THINK_RTAUDIO_VERSION}")
endif()

# MIDI input. Covers ALSA sequencer and JACK MIDI on Linux, CoreMIDI on
# macOS and the Windows multimedia API, which has no file-descriptor story
# at all -- hence the Dispatcher handoff in gthMidiQueue.
#
# Obtained the same way as RtAudio above, and for the same reason: the
# distribution cannot be relied on to have 6.x.
set(THINK_RTMIDI_VERSION 6.0.0)
# We use the commit SHA in case the tag ever gets forced push maliciously
set(THINK_RTMIDI_VERSION_TAG 1e5b49925aa60065db52de44c366d446a902547b)

if(NOT THINK_FETCH_RT)
  pkg_check_modules(RTMIDI IMPORTED_TARGET "rtmidi>=6.0.0")
endif()

if(RTMIDI_FOUND)
  set(THINK_RTMIDI_LIB PkgConfig::RTMIDI)
  set(THINK_RTMIDI_ORIGIN "system ${RTMIDI_VERSION}")
else()
  message(STATUS "rtmidi not found via pkg-config; fetching ${THINK_RTMIDI_VERSION}")

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)

  # Same reasoning as RtAudio above.
  if(APPLE)
    set(RTMIDI_API_CORE ON  CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
  elseif(WIN32)
    set(RTMIDI_API_WINMM ON  CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK  OFF CACHE BOOL "" FORCE)
  else()
    set(RTMIDI_API_ALSA ON CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK ${THINK_PROBE_JACK_FOUND} CACHE BOOL "" FORCE)
  endif()
  set(RTMIDI_TARGETNAME_UNINSTALL "rtmidi-uninstall" CACHE STRING "" FORCE)

  if(APPLE)
    set(RTMIDI_API_CORE ON  CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
  elseif(WIN32)
    set(RTMIDI_API_WINMM ON  CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK  OFF CACHE BOOL "" FORCE)
  else()
    set(RTMIDI_API_ALSA ON CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK ${THINK_PROBE_JACK_FOUND} CACHE BOOL "" FORCE)
  endif()

  # Same as RtAudio above: its install rules are pointless for a statically
  # linked dependency and would land in ours.
  FetchContent_Declare(rtmidi
      GIT_REPOSITORY https://github.com/thestk/rtmidi.git
      GIT_TAG ${THINK_RTMIDI_VERSION_TAG}
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL)

  FetchContent_MakeAvailable(rtmidi)

  set(THINK_RTMIDI_LIB rtmidi)
  set(THINK_RTMIDI_ORIGIN "fetched ${THINK_RTMIDI_VERSION}")
endif()

# The tiled panes the main window is made of. No distribution packages
# mullion-gtk yet, so a system copy is used when pkg-config finds one (a
# Flatpak module, a packager's build) and otherwise it is fetched at a pinned
# commit and built static into the program, the same as RtAudio: nothing new
# to bundle on macOS or Windows and nothing missing from the Linux tarball.
set(THINK_MULLION_VERSION 0.3.0)
set(THINK_MULLION_VERSION_TAG c7e2991d4af32b8870e85f916a0fcc6ed64ec5d1) # v0.3.0

option(THINK_FETCH_MULLION "Always build mullion-gtk from source" OFF)

if(NOT THINK_FETCH_MULLION)
  pkg_check_modules(MULLION IMPORTED_TARGET "mullion-gtk-0>=${THINK_MULLION_VERSION}")
endif()

# Asked again rather than trusting MULLION_FOUND alone: pkg-config's answer
# is cached, so a build directory that once found a system copy would go on
# using it after the fetch was asked for.
if(MULLION_FOUND AND NOT THINK_FETCH_MULLION)
  set(THINK_MULLION_LIB PkgConfig::MULLION)
  set(THINK_MULLION_ORIGIN "system ${MULLION_VERSION}")
else()
  message(STATUS "mullion-gtk not found via pkg-config; fetching ${THINK_MULLION_VERSION}")

  # A C library in a C++ project; its own project() says so too, but a
  # language is best enabled where the whole build can see it.
  enable_language(C)

  # Static, no tests and no install rules: those are its defaults when it is
  # not the top-level project, spelled out so a cache left by another build
  # cannot change them.
  set(MLN_SHARED OFF CACHE BOOL "" FORCE)
  set(MLN_TESTS OFF CACHE BOOL "" FORCE)
  set(MLN_INSTALL OFF CACHE BOOL "" FORCE)

  if(CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR
        "Building mullion-gtk from source needs CMake 3.21 or newer. "
        "Upgrade CMake, or install mullion-gtk and point PKG_CONFIG_PATH at it.")
  endif()

  # No GIT_SHALLOW: a shallow clone can only name a branch or a tag, and
  # this names a commit.
  FetchContent_Declare(mullion_gtk
      GIT_REPOSITORY https://github.com/mishan/mullion-gtk.git
      GIT_TAG ${THINK_MULLION_VERSION_TAG}
      EXCLUDE_FROM_ALL)

  FetchContent_MakeAvailable(mullion_gtk)

  set(THINK_MULLION_LIB mullion-gtk::mullion-gtk)
  set(THINK_MULLION_ORIGIN "fetched ${THINK_MULLION_VERSION}")
endif()

pkg_check_modules(JACK IMPORTED_TARGET jack)
