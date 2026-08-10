# thinksynth -- the application.
#
# src/Makefile.in built src/gui into a gth.a archive and linked it in; that
# split existed only so the gui/ directory could have its own Makefile. Here
# it is one target, and the GTK-free graph model (NodeGraph, NodeEdit,
# NodeLayout, NodeCatalog) is a separate object library because scripts/ links
# those sources directly without wanting gtkmm.

# Deliberately GTK-free so the harnesses can exercise it headlessly -- see the
# comment in scripts/Makefile.
add_library(think_nodemodel OBJECT
    NodeGraph.cpp
    NodeEdit.cpp
    NodeLayout.cpp
    NodeCatalog.cpp)

target_include_directories(think_nodemodel PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(think_nodemodel PUBLIC think)

set(THINKSYNTH_SOURCES
    main.cpp
    gthPrefs.cpp
    gthPatchfile.cpp
    gthDummyAudio.cpp
    gthSynthSource.cpp
    gthRtAudio.cpp
    gui/AboutBox.cpp
    gui/ArgTable.cpp
    gui/Keyboard.cpp
    gui/KeyboardWindow.cpp
    gui/MainSynthWindow.cpp
    gui/MidiMap.cpp
    gui/NodeCanvas.cpp
    gui/NodePalette.cpp
    gui/NodeParams.cpp
    gui/NodeWindow.cpp
    gui/PatchSelWindow.cpp)

# Only the MIDI half of the ALSA backend is left; gthALSAAudio is gone,
# replaced by RtAudio's ALSA support.
if(HAVE_ALSA)
  list(APPEND THINKSYNTH_SOURCES gthALSAMidi.cpp)
endif()

add_executable(thinksynth
    ${THINKSYNTH_SOURCES}
    $<TARGET_OBJECTS:think_nodemodel>)

target_include_directories(thinksynth PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(thinksynth PRIVATE
    think
    PkgConfig::GTKMM
    PkgConfig::GTHREAD
    ${THINK_RTAUDIO_LIB})

if(HAVE_ALSA)
  target_link_libraries(thinksynth PRIVATE PkgConfig::ALSA)
endif()

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

# The app dlopens plugins at parse time, so they have to exist before it is
# worth running -- but not before it links.
add_dependencies(thinksynth plugins)

install(TARGETS thinksynth RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
