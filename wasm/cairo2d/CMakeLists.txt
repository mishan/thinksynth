# cairo2d: cairo's API, recorded and replayed on a Canvas2D.
#
# One object library and one test. Nothing here looks outside this
# directory, and nothing outside it is on the include path -- a user puts
# this directory on theirs and includes <cairo.h> as they would cairo's.
#
# On its own:
#
#   cmake -S wasm/cairo2d -B build-cairo2d && cmake --build build-cairo2d
#   ctest --test-dir build-cairo2d
#
# Inside another build, which is how thinksynth uses it:
#
#   add_subdirectory(<here> cairo2d)
#   target_link_libraries(mytarget PRIVATE cairo2d)
#
# The library is an object library rather than an archive: the drawing
# code's calls are the only references to most of it, and a linker that
# drops an archive member nobody in the main object called would drop the
# half a plugin uses.

cmake_minimum_required(VERSION 3.20)

project(cairo2d VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CAIRO2D_TESTS "Build cairo2d's own test" "${PROJECT_IS_TOP_LEVEL}")

add_library(cairo2d OBJECT cairo2d.cpp)

target_include_directories(cairo2d PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT CAIRO2D_TESTS)
  return()
endif()

add_executable(cairo2dtest test/cairo2dtest.cpp)
target_link_libraries(cairo2dtest PRIVATE cairo2d)

# The two halves of the test. The C++ one is a program; the JavaScript one
# runs that program with --dump and holds replay.js against what comes
# out, so it needs node and the path to the binary -- which under
# Emscripten is a .js that node runs, and replaytest.mjs works that out
# from the suffix.
find_program(CAIRO2D_NODE NAMES node nodejs)

include(CTest)

add_test(NAME cairo2d COMMAND cairo2dtest)

if(CAIRO2D_NODE)
  add_test(NAME cairo2d-replay
           COMMAND "${CAIRO2D_NODE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/test/replaytest.mjs"
                   "$<TARGET_FILE:cairo2dtest>")
else()
  message(STATUS
      "cairo2d: no node, so replay.js is not checked against the recorder.")
endif()
