# HID controllers as instruments: what a device's controls are, and what a
# profile does with them.
#
# Built on the composer host's profile reader (src/thcHidBlock.h), never the
# other way round, so the composer can carry a mapping without this library.
add_library(think_hid OBJECT
    thhControls.cpp
    thhIndex.cpp
    thhMapper.cpp)

target_include_directories(think_hid PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(think_hid PUBLIC think)
