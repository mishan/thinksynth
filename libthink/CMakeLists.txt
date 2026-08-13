# libthink -- the synth engine, the arg system, MIDI, and the DSP-language
# parser.

# bison_target/flex_target replace the whole build.mk dance, including the
# `--defines=' fix and the .SECONDARY that stopped make deleting the generated
# sources on every build. Generated output goes to the build tree, so the
# source tree stays clean -- nothing writes libthink/thinklang.{cpp,h} or
# thinklex.cpp there any more.

bison_target(ThinkParser
             thinklang.yy
             "${CMAKE_CURRENT_BINARY_DIR}/thinklang.cpp"
             DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/thinklang.h")

flex_target(ThinkLexer
            thinklex.ll
            "${CMAKE_CURRENT_BINARY_DIR}/thinklex.cpp")

add_flex_bison_dependency(ThinkLexer ThinkParser)

set(THINK_SOURCES
    thArg.cpp
    thMidiChan.cpp
    thMidiNote.cpp
    thSynthTree.cpp
    thNode.cpp
    thProbe.cpp
    thPlugin.cpp
    thPluginManager.cpp
    thMidiControllerConnection.cpp
    thMidiController.cpp
    thSynth.cpp
    thUtil.cpp
    thDynLib.cpp
    ${BISON_ThinkParser_OUTPUTS}
    ${FLEX_ThinkLexer_OUTPUTS})

# Compiled once, linked into both the shared library and the static archive.
add_library(think_objects OBJECT ${THINK_SOURCES})

target_include_directories(think_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")   # generated thinklang.h, via parser.h

target_link_libraries(think_objects PUBLIC PkgConfig::SIGC)

# THINK_API is dllexport while building the library, dllimport for everyone
# else. Hidden-by-default makes a forgotten THINK_API fail here rather than
# only on Windows -- see the comment in thExport.h.
target_compile_definitions(think_objects PRIVATE THINK_BUILDING_LIB)

set_target_properties(think_objects PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

add_library(think SHARED $<TARGET_OBJECTS:think_objects>)

target_include_directories(think PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(think PUBLIC PkgConfig::SIGC)

if(THINK_DL_LIBRARY)
  target_link_libraries(think PUBLIC ${THINK_DL_LIBRARY})
endif()

# libthink.so.6.0 with an soname of libthink.so.6, which is what the old
# lib_revision scheme produced.
set_target_properties(think PROPERTIES
    OUTPUT_NAME think
    VERSION   "${THINK_LIB_MAJOR}.${THINK_LIB_MINOR}"
    SOVERSION "${THINK_LIB_MAJOR}")

if(THINK_BUILD_STATIC_LIB)
  add_library(think_static STATIC $<TARGET_OBJECTS:think_objects>)
  target_include_directories(think_static PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(think_static PUBLIC PkgConfig::SIGC)
  set_target_properties(think_static PROPERTIES OUTPUT_NAME think)
endif()

# On Windows the DLL is the RUNTIME artifact and the import library is the
# ARCHIVE one; naming only LIBRARY would package neither. ARCHIVE goes to the
# ordinary libdir because it is only of interest to someone linking against
# libthink, not to someone running thinksynth.
install(TARGETS think
        LIBRARY DESTINATION "${THINK_PKG_LIB_DIR}"
        RUNTIME DESTINATION "${THINK_PKG_LIB_DIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

if(THINK_BUILD_STATIC_LIB)
  install(TARGETS think_static ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

# parser.h includes thinklang.h, so an installed libthink needs the generated
# header too -- but it now lives in the build tree. The glob has to exclude
# the generated names explicitly, or a tree that still has an autotools build
# lying around installs those stale copies instead.
file(GLOB THINK_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(FILTER THINK_HEADERS EXCLUDE REGEX "/(thinklang|thinklex)\\.h$")

install(FILES ${THINK_HEADERS}
              "${CMAKE_CURRENT_BINARY_DIR}/thinklang.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/libthink")
