# HID controllers as instruments: what a device's controls are, and what a
# profile does with them.
#
# Built on the composer host's profile reader (src/thcHidBlock.h), never the
# other way round, so the composer can carry a mapping without this library.
add_library(think_hid OBJECT
    thhCamera.cpp
    thhControls.cpp
    thhHands.cpp
    thhIndex.cpp
    thhMapper.cpp
    thhPane.cpp
    thhPlay.cpp)

target_include_directories(think_hid PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src")

# Reading a controller from hidraw is Linux's; the rest builds anywhere.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(think_hid PRIVATE thhIndexHidraw.cpp thhReader.cpp)
  target_link_libraries(think_hid PUBLIC ZLIB::ZLIB)
endif()

find_package(Threads REQUIRED)
target_link_libraries(think_hid PUBLIC think Threads::Threads)
