# thinksynth
#
# Replaces autotools + the hand-rolled build.mk. See PORTING.md, "CMake".
#
# It began as a deliberately faithful reproduction of what configure.ac and
# build.mk did rather than an improvement on it, so that the two builds
# produced the same artefacts and the same dspcheck/dsplevel results and
# anything that broke later was attributable to one change rather than two.
# Autotools is gone now, and the cleanups that reproduction was holding back
# -- dropping -ffast-math, linking plugins against libthink, C++17,
# visibility -- have all landed.

cmake_minimum_required(VERSION 3.16)

project(thinksynth
        VERSION 0.1
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakeDependentOption)
include(CheckIncludeFileCXX)
include(CheckCXXSourceCompiles)
include(TestBigEndian)

# ---------------------------------------------------------------------------
# Library version
#
# Bump lib_major when libthink's ABI changes. ARCHITECTURE.md records what goes
# wrong when a stale copy with an older soname is left in /usr/local/lib.
# ---------------------------------------------------------------------------

set(THINK_LIB_MAJOR 6)
set(THINK_LIB_MINOR 0)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(THINK_ENABLE_DEBUG "Print debug messages to stdout" OFF)
option(THINK_BUILD_HARNESSES "Build the scripts/ test harnesses" ON)
option(THINK_BUILD_STATIC_LIB "Also build libthink as a static archive" ON)

# configure.ac:108 put -ffast-math on every object *and* the link line, which
# sets FTZ/DAZ process-wide -- including for the GUI, and including for the
# link, which is where it pulls in crtfastmath.o and makes the setting a
# property of the whole process rather than of the DSP code that might have
# wanted it. Nothing in the tree depends on it; see the commit that flipped
# this default for the measured effect on the corpus.
option(THINK_FAST_MATH "Build with -ffast-math (as autotools did)" OFF)

# Sanitizers are applied globally rather than per-target on purpose. An
# earlier round of measurements was worthless because the flags reached only
# the harness and not libthink or the plugins -- see AUDIO.md, "The
# harnesses".
set(THINK_SANITIZE "" CACHE STRING
    "Sanitizers to build everything with, e.g. address,undefined or thread")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  # autotools defaulted to -O2 with no -g; RelWithDebInfo is -O2 plus symbols.
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# C++17 for std::filesystem, which replaces the dirent.h walks and the
# strrchr('/') path arithmetic. thRing.h's <atomic> already required C++11.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# libthink is found next to the binary in the build tree and via the install
# prefix afterwards, which is what src/Makefile.in's $ORIGIN rpath did.
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)

add_compile_options(-Wall)

if(THINK_FAST_MATH)
  add_compile_options(-ffast-math)
endif()

if(THINK_ENABLE_DEBUG)
  add_compile_options(-W -Wno-unused-parameter)
endif()

if(THINK_SANITIZE)
  add_compile_options(-fsanitize=${THINK_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${THINK_SANITIZE})
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SIGC   REQUIRED IMPORTED_TARGET sigc++-3.0)
# A floor, so a toolkit that is too old says so once instead of failing in
# three hundred places. 4.6 is the oldest release the code knowingly uses
# anything from (Gtk::ScrolledWindow::unset_child); CI exercises 4.14, which
# is what Ubuntu 24.04 carries, and that is the version this is actually
# tested against.
pkg_check_modules(GTKMM  REQUIRED IMPORTED_TARGET gtkmm-4.0>=4.6)
pkg_check_modules(GTHREAD REQUIRED IMPORTED_TARGET gthread-2.0)
pkg_check_modules(GLIBMM REQUIRED IMPORTED_TARGET glibmm-2.68)

# Named on its own rather than taken transitively through gtkmm, because the
# visual plugins link cairo *without* linking gtkmm -- and because visualcheck
# draws to an image surface with no toolkit and no display at all. Nothing in
# libthink or in the 62 DSP plugins sees this. See src/thVisual.h.
pkg_check_modules(CAIRO  REQUIRED IMPORTED_TARGET cairo)

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

pkg_check_modules(JACK IMPORTED_TARGET jack)

# What -d selects when nothing says otherwise. Empty means "let RtAudio
# choose", which is the right default now that one backend covers everything.
set(THINK_DEFAULT_OUTPUT "" CACHE STRING
    "Default audio API: alsa, jack, pulse, core, wasapi, none, or empty")

# ---------------------------------------------------------------------------
# Platform probes
# ---------------------------------------------------------------------------

check_include_file_cxx(dlfcn.h HAVE_DLFCN_H)

if(NOT APPLE)
  # AC_SEARCH_LIBS(dlopen, [dl c_r]) -- glibc 2.34+ folded libdl into libc, so
  # find_library may legitimately come up empty. Only fail if dlopen is
  # genuinely unavailable.
  find_library(THINK_DL_LIBRARY dl)
  if(NOT THINK_DL_LIBRARY)
    set(THINK_DL_LIBRARY "")
  endif()
endif()

set(CMAKE_REQUIRED_LIBRARIES ${THINK_DL_LIBRARY})
check_cxx_source_compiles("
  #include <dlfcn.h>
  int main() { dlerror(); return 0; }
" HAVE_DLERROR)
unset(CMAKE_REQUIRED_LIBRARIES)

check_cxx_source_compiles("
  #include <signal.h>
  int main() { sighandler_t h = 0; (void)h; return 0; }
" THINK_HAVE_SIGHANDLER_T)

if(NOT THINK_HAVE_SIGHANDLER_T)
  check_cxx_source_compiles("
    #include <signal.h>
    int main() { sig_t h = 0; (void)h; return 0; }
  " THINK_HAVE_SIG_T)
  if(THINK_HAVE_SIG_T)
    set(USE_SIG_T 1)
  else()
    set(GUESS_SIG_T 1)
  endif()
endif()

test_big_endian(THINK_BIG_ENDIAN)
if(THINK_BIG_ENDIAN)
  set(WORDS_BIGENDIAN 1)
endif()

if(APPLE)
  set(USING_DARWIN 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set(USING_FREEBSD 1)
endif()

if(THINK_ENABLE_DEBUG)
  set(USE_DEBUG 1)
endif()

# ---------------------------------------------------------------------------
# Install layout, and the paths baked into config.h
#
# The trailing slashes matter: the sources concatenate a bare filename onto
# each of these.
# ---------------------------------------------------------------------------

if(WIN32)
  set(THINK_PLUGIN_SUFFIX ".dll")
elseif(APPLE)
  set(THINK_PLUGIN_SUFFIX ".dylib")
else()
  set(THINK_PLUGIN_SUFFIX ".so")
endif()

set(THINK_PLUGIN_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME}/plugins/")
set(THINK_DSP_PATH    "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/dsp/")
set(THINK_PATCH_PATH  "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/patches/")
set(THINK_DEFAULT_THINKRC "${CMAKE_INSTALL_FULL_SYSCONFDIR}/thinkrc")

# The application ID: the GApplication id, the desktop entry and metainfo
# stems, the macOS bundle identifier, and the Flatpak app-id are all this one
# string. They have to agree -- a GApplication id that differs from the
# Flatpak app-id fails to own its name on the session bus, which takes
# desktop integration and the portals with it -- and the way to make them
# agree is to have one of them.
#
# Compiled in rather than read from FLATPAK_ID at run time, because it is
# equally the identity outside a sandbox, where there is no FLATPAK_ID.
set(THINK_APP_ID "org.thinksynth.thinksynth")

# Who published this, and where it lives. Named here because more than one
# thing wants them: the Windows version block behind File -> Properties, and
# CPACK_PACKAGE_VENDOR further down, which used to carry the vendor string on
# its own.
set(THINK_APP_VENDOR "Metaphonic Labs")
set(THINK_APP_URL    "https://github.com/mishan/thinksynth")

configure_file(cmake/config.h.in "${PROJECT_BINARY_DIR}/config.h")

# Every translation unit does #include "config.h", unqualified.
include_directories("${PROJECT_BINARY_DIR}")

# ---------------------------------------------------------------------------

# Windows has no rpath. A .exe finds a DLL next to itself or on PATH, and
# nothing else -- so libthink.dll sitting in build/libthink/ is invisible to
# build/src/thinksynth.exe. On Linux and macOS the build tree works because
# CMake writes an rpath into every binary it links; there is no equivalent to
# write here.
#
# The symptom is unhelpful: MSYS2's loader fails with ENOENT, so bash reports
# "No such file or directory" and exits 127, for a file that plainly exists.
#
# $<TARGET_RUNTIME_DLLS> is CMake's answer -- the DLLs of the target's linked
# dependencies, which it copies next to the executable after linking. It does
# not cover the GTK stack, which comes from PATH under MSYS2 and from the
# packaging step for a release, and it needs CMake 3.21.
function(think_copy_runtime_dlls target)
  if(NOT WIN32 OR CMAKE_VERSION VERSION_LESS 3.21)
    return()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS
      VERBATIM)
endfunction()

if(WIN32 AND CMAKE_VERSION VERSION_LESS 3.21)
  message(WARNING
      "CMake ${CMAKE_VERSION} cannot copy runtime DLLs into the build tree "
      "(3.21+ needed), so nothing built here will start without libthink.dll "
      "on PATH.")
endif()

# Decides where plugins, DSPs and libthink go on each platform. Must precede
# the subdirectories, whose install() rules use it.
include(Layout)

enable_testing()

add_subdirectory(libthink)
add_subdirectory(plugins)
add_subdirectory(src)

if(THINK_BUILD_HARNESSES)
  add_subdirectory(scripts)
endif()

install(DIRECTORY dsp/     DESTINATION "${THINK_PKG_DATA_DIR}/dsp"
        FILES_MATCHING PATTERN "*.dsp")
install(DIRECTORY patches/ DESTINATION "${THINK_PKG_DATA_DIR}/patches"
        FILES_MATCHING PATTERN "*.patch")

# ---------------------------------------------------------------------------
# Freedesktop desktop entry, icon and AppStream metainfo.
#
# Only meaningful on a free desktop: macOS takes the same information from the
# .app's Info.plist and Windows from the executable, so installing them there
# would put files inside the bundle that nothing ever reads.
#
# These are what make the Flatpak an application rather than a binary in a
# sandbox. Without the desktop entry it has no menu item and no icon; without
# the metainfo no software centre can describe it. They are installed for an
# ordinary install too, which is where they belonged in any case.
#
# The file stem is the application ID and has to be: the desktop entry names
# its icon by that ID, the metainfo's <launchable> names the desktop entry,
# and Flatpak exports only files named for the app's own ID.
# ---------------------------------------------------------------------------

if(NOT APPLE AND NOT WIN32)
  install(FILES "data/${THINK_APP_ID}.desktop"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

  install(FILES "data/${THINK_APP_ID}.svg"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

  install(FILES "data/${THINK_APP_ID}.metainfo.xml"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
endif()

# ---------------------------------------------------------------------------
# The two generated data files that used to come from etc/Makefile.in and
# docs/Makefile.in.
#
# Their templates still spell substitutions the way configure did -- @dsp_path@
# rather than @THINK_DSP_PATH@ -- so the variables are aliased here instead of
# rewriting files whose contents are not really ours to reformat.
# ---------------------------------------------------------------------------

set(dsp_path       "${THINK_DSP_PATH}")
set(patch_path     "${THINK_PATCH_PATH}")
set(plugin_path    "${THINK_PLUGIN_PATH}")
set(thinkrc        "${THINK_DEFAULT_THINKRC}")
set(DEFAULT_OUTPUT "${THINK_DEFAULT_OUTPUT}")
set(PACKAGE_VERSION "${PROJECT_VERSION}")

configure_file(etc/thinkrc.in        "${PROJECT_BINARY_DIR}/etc/thinkrc" @ONLY)
configure_file(docs/thinksynth.1.in  "${PROJECT_BINARY_DIR}/docs/thinksynth.1" @ONLY)

# A system-wide config file and a man page are Unix notions; a .app and a
# zip have nowhere sensible to put either.
if(NOT APPLE AND NOT WIN32)
  install(FILES "${PROJECT_BINARY_DIR}/etc/thinkrc"
          DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}")

  install(FILES "${PROJECT_BINARY_DIR}/docs/thinksynth.1"
          DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()

include(Packaging)

# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  system ................: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  build type ............: ${CMAKE_BUILD_TYPE}")
message(STATUS "  RtAudio ...............: ${THINK_RTAUDIO_ORIGIN}")
message(STATUS "  RtMidi ................: ${THINK_RTMIDI_ORIGIN}")
message(STATUS "  default output ........: ${THINK_DEFAULT_OUTPUT}")
message(STATUS "  debug .................: ${THINK_ENABLE_DEBUG}")
message(STATUS "  -ffast-math ...........: ${THINK_FAST_MATH}")
message(STATUS "  sanitizers ............: ${THINK_SANITIZE}")
message(STATUS "  libthink soversion ....: ${THINK_LIB_MAJOR}")
message(STATUS "  plugins install to ....: ${THINK_PLUGIN_PATH}")
message(STATUS "")
