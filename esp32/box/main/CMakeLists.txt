# The voice and its profile, written by dspcompile from the patch (see
# README.md), the mapper and the BOX's controls from hid/, and the kernels.
set(T "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(
    SRCS "box.cpp" "audio.c"
         "${T}/hid/thhMapper.cpp" "${T}/hid/thhControls.cpp" "${T}/hid/thhBox.cpp"
    INCLUDE_DIRS "." "${T}/hid" "${T}/src" "${T}/libthink" "${T}/plugins"
    REQUIRES esp_driver_i2s esp_driver_i2c esp_timer esp_lcd)
# The mapper and the touch driver both leave members to their defaults.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers
                       $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
