# The voice and its profile, written by dspcompile from the patch (see
# README.md), the mapper and the BOX's controls from hid/, and the kernels;
# and the .gen composer (composer.cmake).
set(T "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(composer "")
set(composerinc "")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  include("${CMAKE_CURRENT_LIST_DIR}/composer.cmake")
  box_composer_sources(composer composerinc)
endif()
# Whole: the plugin table refers to each composer's entry points weakly,
# and a weak reference does not pull an archive member in.
idf_component_register(
    SRCS "box.cpp" "composer.cpp"
         "${T}/hid/thhMapper.cpp" "${T}/hid/thhControls.cpp" "${T}/hid/thhBox.cpp"
         "${T}/game/thgMarbles.cpp" "${T}/game/thgLife.cpp" ${composer}
    INCLUDE_DIRS "." "${T}/hid" "${T}/game" "${T}/src" "${T}/libthink" "${T}/plugins"
                 ${composerinc}
    WHOLE_ARCHIVE
    EMBED_TXTFILES "../pieces/bells.gen"
    REQUIRES boxsim esp_timer heap freertos)
# The mapper and the touch driver both leave members to their defaults.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers
                       $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    THINK_STATIC_PLUGINS THINK_STATIC THC_NO_DRAW)
