# The voice and its profile, written by dspcompile from the patch (see
# README.md), the mapper and the BOX's controls from hid/, and the kernels.
set(T "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(
    SRCS "box.cpp"
         "${T}/hid/thhMapper.cpp" "${T}/hid/thhControls.cpp" "${T}/hid/thhBox.cpp"
         "${T}/game/thgMarbles.cpp" "${T}/game/thgLife.cpp"
    INCLUDE_DIRS "." "${T}/hid" "${T}/game" "${T}/src" "${T}/libthink" "${T}/plugins"
    REQUIRES boxsim esp_timer heap freertos)
# The mapper and the touch driver both leave members to their defaults.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers
                       $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
