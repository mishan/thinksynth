# The browser build's cmake/ThinkPlugin.cmake.
#
# An AudioWorkletGlobalScope has no file system and nothing to dlopen with,
# so here a plugin is not a module at all. It is compiled into the one wasm
# file, inside a namespace of its own -- every plugin defines the same
# handful of names, and one module can hold only one of each -- and listed
# in the table thDynLib's static branch looks names up in. The plugin list
# is still plugins/CMakeLists.txt's, read as it stands.
#
# Both ABIs come this way now: the DSP plugins a .dsp names, and the
# composers a .gen names, since the scheduler runs in the worklet too. What
# each contributes is a wrapper source and a row in the table; CMakeLists.txt
# here writes the table from the two properties below.
#
# The visuals come this way too now: there is something to draw on, which is
# cairo-canvas2d, and something to draw -- the probes the node editor arms.

# A wrapper source, and a name to find its row by.
#
# The DSP plugins' exports are plain C++ names, so a namespace is the whole
# trick. See think_add_composer for why the other ABI needs one more.
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

# A composer, likewise -- with the renames a second ABI turns out to need.
#
# A composer's exports are declared `extern "C"', and that is a linkage and
# not a scope: sixteen namespaces would still be sixteen definitions of
# `composer_init'. So each export is #defined to a name of its own ahead of
# the include, and the table looks the plugin up under those. The namespace
# stays, for everything else the file has at file scope.
#
# Which exports this one has is not read here. The table declares every
# name a composer may export, weak, and one the plugin did not define comes
# out null (CMakeLists.txt, where the table is written): what the plugin
# compiled decides, not how it spelled it. One that has lost a mandatory
# entry point is refused at load, by thcPlugin, exactly as a module missing
# it would be. THINK_COMPOSER_EXPORTS and the prototypes are CMakeLists.txt's.
#
# composer_draw comes this way too now. It used to be the one export that
# could not -- a worklet has no canvas and there was no cairo to link, so the
# composers were compiled with THC_NO_DRAW and the eight that draw left both
# the function and their <cairo.h> out. cairo-canvas2d is the cairo they link
# now: the same calls, recorded into a list the page replays. Its cairo.h is
# what thcstatic.h includes, so the plugin's own `#include <cairo.h>' finds
# it already behind its guard.
function(think_add_composer name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/composer/${name}.cpp")
  set(wrapper "${PROJECT_BINARY_DIR}/static/composer_${name}.cpp")

  set(defines "")

  foreach(ex IN LISTS THINK_COMPOSER_EXPORTS)
    string(APPEND defines "#define composer_${ex} thc_${name}_${ex}\n")
  endforeach()

  file(CONFIGURE OUTPUT "${wrapper}" CONTENT
"/* plugins/composer/${name}.cpp, in a namespace of its own and under export
   names of its own. Written by wasm/web/cmake/ThinkPlugin.cmake; see
   wasm/web/thcstatic.h. */
#include \"thcstatic.h\"

${defines}
namespace thc_${name} {
#include \"${src}\"
}
")

  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_SOURCES "${wrapper}")
  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_COMPOSERS "${name}")

  # plugins/CMakeLists.txt links cairo into eight of these by target name,
  # and a name that is no target is an error. An object library nothing
  # depends on and nothing builds is the cheapest thing that answers to one
  # -- the include path the draws actually compile against is thinkweb's,
  # and it has cairo2d on it.
  set(nothing "${PROJECT_BINARY_DIR}/static/nothing.cpp")
  file(CONFIGURE OUTPUT "${nothing}" CONTENT "")
  add_library(composer_${name} OBJECT EXCLUDE_FROM_ALL "${nothing}")
endfunction()

# A visual module, the same way. Its exports are `extern "C"' like a
# composer's, so each is renamed ahead of the include and the table looks
# the module up under the renamed names; what the host asks for is the
# plain spelling, and thDynLib's static branch answers from the row.
#
# fftr.h comes along inside the namespace, which is where it belongs: it is
# the modules' own header and not a shared one, so two modules that both
# use it get a copy each of what it defines.
function(think_add_visual name)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/visual/${name}.cpp")
  set(wrapper "${PROJECT_BINARY_DIR}/static/visual_${name}.cpp")

  set(defines "")

  foreach(ex IN LISTS THINK_VISUAL_EXPORTS)
    string(APPEND defines "#define visual_${ex} thv_${name}_${ex}\n")
  endforeach()

  file(CONFIGURE OUTPUT "${wrapper}" CONTENT
"/* plugins/visual/${name}.cpp, in a namespace of its own and under export
   names of its own. Written by wasm/web/cmake/ThinkPlugin.cmake; see
   wasm/web/thvstatic.h. */
#include \"thvstatic.h\"

${defines}
namespace thv_${name} {
#include \"${src}\"
}
")

  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_SOURCES "${wrapper}")
  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_VISUALS "${name}")
endfunction()
