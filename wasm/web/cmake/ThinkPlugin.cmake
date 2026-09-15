# The browser build's cmake/ThinkPlugin.cmake.
#
# An AudioWorkletGlobalScope has no file system and nothing to dlopen with,
# so here a plugin is not a module at all. It is compiled into the one wasm
# file, inside a namespace of its own -- every plugin defines module_init,
# args[] and mystate, and one module can hold only one of each -- and listed
# in the table thDynLib's static branch looks names up in. The plugin list is
# still plugins/CMakeLists.txt's, read as it stands.
#
# Only the DSP plugins. The composers belong to the scheduler, which reaches
# the browser in M2 (JAM.md); the visuals draw, and there is nothing to draw
# on.

function(think_add_plugin category name)
  set(id "${category}_${name}")
  set(wrapper "${PROJECT_BINARY_DIR}/static/${id}.cpp")

  # CMAKE_CURRENT_SOURCE_DIR is the caller's, plugins/.
  file(CONFIGURE OUTPUT "${wrapper}" CONTENT
"/* plugins/${category}/${name}.cpp, in a namespace of its own. Written by
   wasm/web/cmake/ThinkPlugin.cmake; see wasm/web/thinkstatic.h. */
#include \"thinkstatic.h\"

namespace thp_${id} {
#include \"${CMAKE_CURRENT_SOURCE_DIR}/${category}/${name}.cpp\"
}
")

  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_SOURCES "${wrapper}")
  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_PLUGINS "${category}/${name}")
endfunction()

function(think_add_plugin_category category)
  foreach(name IN LISTS ARGN)
    think_add_plugin("${category}" "${name}")
  endforeach()
endfunction()

# Not built, but named: plugins/CMakeLists.txt links cairo into eight of the
# composers by target name, and a name that is no target is an error. An
# object library nothing depends on and nothing builds is the cheapest
# thing that answers to one.
function(think_add_composer name)
  set(nothing "${PROJECT_BINARY_DIR}/static/nothing.cpp")
  file(CONFIGURE OUTPUT "${nothing}" CONTENT "")
  add_library(composer_${name} OBJECT EXCLUDE_FROM_ALL "${nothing}")
endfunction()

function(think_add_visual name)
endfunction()
