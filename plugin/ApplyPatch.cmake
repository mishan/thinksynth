# Applies a patch to the source tree it runs in, once: a patch that is
# already in -- which is what FetchContent finds when it runs its patch
# step again over a tree it fetched before -- is left as it is.
#
#   cmake -DPATCH=<file> -P ApplyPatch.cmake

execute_process(COMMAND git apply --reverse --check "${PATCH}"
                RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)

if(applied EQUAL 0)
  return()
endif()

execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ApplyPatch.cmake: ${PATCH} does not apply")
endif()
