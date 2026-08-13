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

set(THINKSYNTH_SOURCES
    main.cpp
    gthPrefs.cpp
    gthTheme.cpp
    gthPatchfile.cpp
    gthDummyAudio.cpp
    gthSynthSource.cpp
    gthGtkRuntime.cpp
    gthRtAudio.cpp
    gthRtMidi.cpp
    gthMidiQueue.cpp
    gui/AboutBox.cpp
    gui/ArgTable.cpp
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
    gui/PatchSelWindow.cpp)

add_executable(thinksynth
    ${THINKSYNTH_SOURCES}
    $<TARGET_OBJECTS:think_nodemodel>
    $<TARGET_OBJECTS:think_visualhost>)

target_include_directories(thinksynth PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(thinksynth PRIVATE
    think
    PkgConfig::GTKMM
    PkgConfig::GTHREAD
    PkgConfig::CAIRO
    ${THINK_RTAUDIO_LIB}
    ${THINK_RTMIDI_LIB})

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
