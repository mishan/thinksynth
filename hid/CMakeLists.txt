# HID controllers as instruments: what a device's controls are, and what a
# profile does with them.
#
# Built on libthink's profile reader (libthink/thHidBlock.h), never the other
# way round, so a .gen or a .dsp can carry a mapping without this library.
add_library(think_hid OBJECT
    thhBox.cpp
    thhCamera.cpp
    thhControls.cpp
    thhHandTrack.cpp
    thhPhone.cpp
    thhHands.cpp
    thhHost.cpp
    thhIndex.cpp
    thhIndexWireless.cpp
    thhLooper.cpp
    thhMapper.cpp
    thhPane.cpp
    thhPlay.cpp
    thhProfile.cpp
    thhForm.cpp
    thhProfileEdit.cpp
    thhSession.cpp)

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
