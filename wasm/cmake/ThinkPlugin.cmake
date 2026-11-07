# The wasm build's cmake/ThinkPlugin.cmake.
#
# plugins/CMakeLists.txt is included as it stands, so the list of what gets
# built lives in one place for both builds; what changes is what a plugin
# is. Here it is an Emscripten side module, and the main module dlopens it
# the way the native host dlopens a .so -- the same four symbol names looked
# up through the same thDynLib seam, with no registry standing in for it.
#
# Side modules link nothing of their own: libc, libc++, libthink and sigc++
# all come from the main module, which exports every symbol it has for them
# (MAIN_MODULE=1). That is the wasm spelling of "every plugin links against
# libthink".

function(think_add_plugin category name)
  set(target "plugin_${category}_${name}")
  add_library(${target} MODULE "${category}/${name}.cpp")
  set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "${name}"
      PREFIX ""
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/${category}"
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  target_compile_definitions(${target} PRIVATE PLUGIN_BUILD)
  target_include_directories(${target} PRIVATE
      "${THINK_TOP}"
      "${THINK_TOP}/libthink"
      "${PROJECT_BINARY_DIR}/libthink")
  target_link_libraries(${target} PRIVATE PkgConfig::SIGC)
  target_link_options(${target} PRIVATE -sSIDE_MODULE=1)
  add_dependencies(plugins ${target})
endfunction()

function(think_add_plugin_category category)
  foreach(name IN LISTS ARGN)
    think_add_plugin("${category}" "${name}")
  endforeach()
endfunction()

function(think_add_composer name)
  set(target "composer_${name}")
  add_library(${target} MODULE "composer/${name}.cpp")
  set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "${name}"
      PREFIX ""
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/composer"
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  target_compile_definitions(${target} PRIVATE COMPOSER_PLUGIN_BUILD)
  target_include_directories(${target} PRIVATE
      "${THINK_TOP}"
      "${THINK_TOP}/libthink")
  target_link_options(${target} PRIVATE -sSIDE_MODULE=1)
  add_dependencies(plugins ${target})
endfunction()

# The visual plugins draw with cairo and make no sound; there is no cairo
# here to draw with and nothing in genwav to draw on.
function(think_add_visual name)
endfunction()
