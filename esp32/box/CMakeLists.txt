# thinksynth on an Espressif ESP32-S3-BOX: patches compiled to voices
# (scripts/dspcompile), played from the BOX's touchscreen and IMU through
# each patch's own controller blocks. See README.md.
cmake_minimum_required(VERSION 3.16)
# boxsim, the BOX's board support and simulator, comes pinned by
# main/idf_component.yml; BOXSIM_DIR, a checkout, takes its place.
if(NOT BOXSIM_DIR)
  set(BOXSIM_DIR "$ENV{BOXSIM_DIR}")
endif()
if(BOXSIM_DIR)
  if(NOT EXISTS "${BOXSIM_DIR}/components/boxsim")
    message(FATAL_ERROR "No boxsim at BOXSIM_DIR, ${BOXSIM_DIR}")
  endif()
  set(EXTRA_COMPONENT_DIRS "${BOXSIM_DIR}/components")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The managed components differ by target, the codec's driver the BOX's
# only, so a lock file each.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
# The linux target is boxsim's simulator: only what main needs, as most
# of IDF has no linux port.
if(IDF_TARGET STREQUAL "linux")
  idf_build_set_property(MINIMAL_BUILD ON)
endif()
project(thinkbox)
