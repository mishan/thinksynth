# thinksynth -- the application.
#
# src/Makefile.in built src/gui into a gth.a archive and linked it in; that
# split existed only so the gui/ directory could have its own Makefile. Here
# it is one target, and the GTK-free graph model (NodeGraph, NodeEdit,
# NodeLayout, NodeCatalog) is a separate object library because scripts/ links
# those sources directly without wanting gtkmm.

# Deliberately GTK-free so the harnesses can exercise it headlessly -- see the
# comment in the harnesses.
add_library(think_nodemodel OBJECT
    NodeGraph.cpp
    NodeEdit.cpp
    NodeLayout.cpp
    NodeCatalog.cpp)

target_include_directories(think_nodemodel PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(think_nodemodel PUBLIC think)

# The host side of the visual plugin ABI: dlopen, look up six symbols, clip and
# call. Its own object library rather than a member of think_nodemodel because
# it needs cairo and the node model does not -- and "GTK-free" above is a claim
# worth keeping narrow enough to stay true.
#
# It links libthink only for thDynLib, the dlopen shim -- there is no reason
# for a second one of those, and PORTING.md records why the first one exists.
# The dependency that matters is the other direction: nothing in libthink and
# none of the 62 DSP plugins sees cairo, and a visual *module* links neither
# libthink nor sigc++, which is what stops a visualizer reaching into the graph
# it is drawing.
add_library(think_visualhost OBJECT thVisual.cpp)

target_include_directories(think_visualhost PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(think_visualhost PUBLIC think PkgConfig::CAIRO)

# The host side of the composer framework: the dlopen wrapper for composer
# modules and the scheduler that drives them. Its own object library for the
# same reason the other two are -- it needs glibmm (the scheduler runs off a
# Glib timeout and the monotonic clock) but not gtkmm, and the replay-
# determinism harness planned in the handoff wants to link exactly this,
# headlessly, without a toolkit.
#
# Deliberately in src/ rather than libthink/: composers are loaded by the
# GUI and never by the parser, and the scheduler touches the synth only
# through the same GUI-thread calls the on-screen keyboard uses. libthink
# stays main-loop-free; this cannot. Same reasoning as think_visualhost,
# with glibmm standing where cairo stood.
add_library(think_composerhost OBJECT
    thcPlugin.cpp
    thcScheduler.cpp
    thcGenFile.cpp
    thcGenEdit.cpp)

target_include_directories(think_composerhost PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(think_composerhost PUBLIC think PkgConfig::GLIBMM)

set(THINKSYNTH_SOURCES
    main.cpp
    gthPrefs.cpp
    gthTheme.cpp
    gthThemePolicy.cpp
    gthPatchfile.cpp
    gthDummyAudio.cpp
    gthSynthSource.cpp
    gthGtkRuntime.cpp
    gthRtAudio.cpp
    gthRtMidi.cpp
    gthMidiQueue.cpp
    gui/AboutBox.cpp
    gthConsole.cpp
    gui/ArgTable.cpp
    gui/ColumnUtil.cpp
    gui/Dialogs.cpp
    gui/Keyboard.cpp
    gui/KeyboardWindow.cpp
    gui/MainSynthWindow.cpp
    gui/MidiMap.cpp
    gui/NodeCanvas.cpp
    gui/NodePalette.cpp
    gui/NodeParams.cpp
    gui/NodeEditor.cpp
    gui/SaveButton.cpp
    gui/PatchSelWindow.cpp
    gui/PianoRoll.cpp
    gui/ComposerCanvas.cpp
    gui/ComposerWindow.cpp)

# ---------------------------------------------------------------------------
# Windows resources: icon, manifest, version block
#
# A MinGW binary has none of these by default -- no icon in Explorer, nothing
# behind File -> Properties -> Details, and no manifest, which means Windows
# assumes the program predates high-DPI displays and bitmap-stretches its
# window on one.
#
# To be clear about what this is not: it is metadata, not a signature. An
# unsigned executable stays unsigned and SmartScreen will still say so.
# ---------------------------------------------------------------------------

if(WIN32)
  # project(VERSION 0.1) leaves PROJECT_VERSION_PATCH empty, and an empty
  # field in a FILEVERSION is a windres syntax error rather than a zero.
  set(THINK_VER_MAJOR "${PROJECT_VERSION_MAJOR}")
  set(THINK_VER_MINOR "${PROJECT_VERSION_MINOR}")
  set(THINK_VER_PATCH "${PROJECT_VERSION_PATCH}")

  foreach(_f THINK_VER_MAJOR THINK_VER_MINOR THINK_VER_PATCH)
    if(NOT ${_f})
      set(${_f} 0)
    endif()
  endforeach()

  enable_language(RC)

  # The script names the .ico and the .manifest by absolute path, because the
  # configured copy lives in the build tree while both of those are in the
  # source tree, and windres resolves a bare filename against its include path
  # rather than against the script that named it. Forward slashes: both windres
  # and rc.exe accept them, and a backslash inside an RC string literal is an
  # escape.
  set(THINK_RC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

  configure_file(thinksynth.rc.in
                 "${CMAKE_CURRENT_BINARY_DIR}/thinksynth.rc" @ONLY)

  list(APPEND THINKSYNTH_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/thinksynth.rc")
endif()

# ---------------------------------------------------------------------------
# macOS bundle icon
#
# Finder draws thinksynth.app with whatever Contents/Resources file the
# Info.plist names in CFBundleIconFile. That is two separate things, and they
# are set in two places on purpose:
#
#   the plist key is a target property, MACOSX_BUNDLE_ICON_FILE, and lives in
#   cmake/Packaging.cmake with the rest of the bundle metadata;
#
#   getting the file itself into Contents/Resources is a *source file*
#   property, and source file properties are scoped to the directory that
#   defines the target -- which is this one. Setting it next to its sibling in
#   Packaging.cmake would be tidier and would silently do nothing.
#
# Like the .ico above, the .icns is committed rather than rendered here, so a
# macOS build needs no rasteriser. scripts/make-icons.py produces both from
# data/org.thinksynth.thinksynth.svg.
# ---------------------------------------------------------------------------

if(APPLE)
  set(THINK_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/thinksynth.icns")

  set_source_files_properties("${THINK_ICNS}" PROPERTIES
                              MACOSX_PACKAGE_LOCATION Resources)

  list(APPEND THINKSYNTH_SOURCES "${THINK_ICNS}")
endif()

add_executable(thinksynth
    ${THINKSYNTH_SOURCES}
    $<TARGET_OBJECTS:think_nodemodel>
    $<TARGET_OBJECTS:think_visualhost>
    $<TARGET_OBJECTS:think_composerhost>)

# The GUI subsystem on Windows, so launching this does not also open a
# terminal. Without it the linker defaults to the console subsystem and
# Windows gives every run a console window whether it wants one or not.
#
# Output is not given up for it: gthAttachConsole() reconnects stdout and
# stderr to the terminal when a terminal is what launched us. See
# src/gthConsole.cpp.
#
# main() still works -- MinGW's runtime provides the WinMain that calls it --
# so this is a link-time change and not a source-level one.
if(WIN32)
  set_target_properties(thinksynth PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(thinksynth PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

# gthTheme.cpp reads AppleInterfaceStyle and subscribes to the distributed
# notification centre, which is CoreFoundation. Nothing else in the tree needs
# a framework, so it is named here rather than anywhere more general.
if(APPLE)
  find_library(THINK_COREFOUNDATION CoreFoundation REQUIRED)
endif()

target_link_libraries(thinksynth PRIVATE
    think
    PkgConfig::GTKMM
    PkgConfig::GTHREAD
    PkgConfig::CAIRO
    ${THINK_RTAUDIO_LIB}
    ${THINK_RTMIDI_LIB}
    ${THINK_COREFOUNDATION})

# acinclude.m4's AC_LINKER_DYNAMIC put -Wl,-export-dynamic,--no-as-needed on
# the link line so that a dlopen'd plugin can resolve against symbols in the
# executable itself. libthink is a shared library, so in practice plugins
# resolve against it rather than against the binary -- but reproducing the
# flag keeps this build equivalent to the one it replaces, and it costs
# nothing.
set_target_properties(thinksynth PROPERTIES ENABLE_EXPORTS ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(thinksynth PRIVATE "LINKER:--no-as-needed")
endif()

# libthink.dll next to thinksynth.exe. See think_copy_runtime_dlls.
think_copy_runtime_dlls(thinksynth)

# The app dlopens plugins at parse time, so they have to exist before it is
# worth running -- but not before it links.
add_dependencies(thinksynth plugins)

# On macOS and Windows the app and its dependency closure are installed by
# cmake/Packaging.cmake, which needs to name the same target in one
# install(TARGETS) call as the RUNTIME_DEPENDENCY_SET.
if(NOT APPLE AND NOT WIN32)
  install(TARGETS thinksynth RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
