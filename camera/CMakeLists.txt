# Webcams as instruments: a reader thread and its frames' luma. What a
# picture's motion is lives in libthink (thMotion), so the browser has it.
add_library(think_camera OBJECT
    thiCamera.cpp
    thiV4l2.cpp)

target_include_directories(think_camera PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(think_camera PUBLIC think JPEG::JPEG Threads::Threads)
