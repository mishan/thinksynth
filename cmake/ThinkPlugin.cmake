# think_add_plugin(<category> <name>)
#
# One dlopen-able module per .cpp, laid out in the build tree exactly as
# thPluginManager expects to find it: <root>/<category>/<name><PLUGIN_SUFFIX>.
#
# MODULE is the right CMake library type here -- it is specifically "a library
# that is dlopen'd rather than linked against", so CMake produces -bundle on
# macOS and a plain .dll on Windows without a PLUGIN_SUFFIX variable having to
# drive the link line.
#
# Plugins link against libthink like any other consumer. They used to link
# against -lm and nothing else and resolve thPlugin::regArg, thSynthTree::getArg
# and the rest against the host process at dlopen time -- which works on Linux,
# worked on macOS only via -flat_namespace -undefined suppress, and cannot work
# on Windows at all. See thExport.h.

function(think_add_plugin category name)
  set(target "plugin_${category}_${name}")

  add_library(${target} MODULE "${category}/${name}.cpp")

  set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "${name}"
      PREFIX ""
      SUFFIX "${THINK_PLUGIN_SUFFIX}"
      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/${category}")

  target_compile_definitions(${target} PRIVATE PLUGIN_BUILD)

  target_include_directories(${target} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/libthink"
      "${PROJECT_BINARY_DIR}/libthink")

  target_link_libraries(${target} PRIVATE think PkgConfig::SIGC m)

  # A plugin's ABI is the four symbols the host looks up by name; thPlugin.h
  # marks those with THINK_PLUGIN_API. Everything else -- desc, mystate, the
  # args[] table -- is private, and hidden-by-default says so.
  set_target_properties(${target} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)

  # A MODULE is a LIBRARY artifact on Unix and a RUNTIME one on Windows, so
  # naming only LIBRARY would install nothing there and leave a package whose
  # plugin directory is empty.
  install(TARGETS ${target}
          LIBRARY DESTINATION "${THINK_PKG_PLUGIN_DIR}/${category}"
          RUNTIME DESTINATION "${THINK_PKG_PLUGIN_DIR}/${category}")

  # So `plugins' can be built on its own, and so the app can depend on the
  # whole set existing.
  add_dependencies(plugins ${target})
endfunction()


# think_add_plugin_category(<category> <name> [<name>...])
function(think_add_plugin_category category)
  foreach(name IN LISTS ARGN)
    think_add_plugin("${category}" "${name}")
  endforeach()
endfunction()


# think_add_composer(<name>)
#
# A composer: turns a clock into note/chanarg events, or transforms them.
# See libthink/thcomposer.h for why this is a third ABI rather than an extra
# entry point on either of the other two.
#
# Like a visual module it does not link libthink -- a composer has no node,
# no arg and no tree, and its whole interface is C structs and function
# pointers. Unlike a visual module it does not even link cairo: composer_draw
# takes a cairo_t* but only the host needs the real type. thcomposer.h
# forward-declares it, so the include path below carries libthink/ solely for
# thcomposer.h and thExport.h.
#
# It lands in <root>/plugins/composer/ alongside the DSP categories, so one
# THINK_PLUGIN_PATH still finds everything.
function(think_add_composer name)
  set(target "composer_${name}")

  add_library(${target} MODULE "composer/${name}.cpp")

  set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "${name}"
      PREFIX ""
      SUFFIX "${THINK_PLUGIN_SUFFIX}"
      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/composer")

  target_compile_definitions(${target} PRIVATE COMPOSER_PLUGIN_BUILD)

  target_include_directories(${target} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/libthink")

  target_link_libraries(${target} PRIVATE m)

  set_target_properties(${target} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)

  install(TARGETS ${target}
          LIBRARY DESTINATION "${THINK_PKG_PLUGIN_DIR}/composer"
          RUNTIME DESTINATION "${THINK_PKG_PLUGIN_DIR}/composer")

  add_dependencies(plugins ${target})
endfunction()


# think_add_visual(<name>)
#
# A visualizer: fed samples, draws with cairo. See src/thVisual.h for why this
# is a separate ABI rather than an extra entry point on a DSP plugin.
#
# A separate function rather than a flag on think_add_plugin because almost
# nothing is shared. A visual module does NOT link libthink -- it has no node,
# no arg and no tree, and keeping the engine out means a visualizer cannot
# accidentally reach into the graph it is drawing. What it does link is cairo,
# and containing that is the whole reason this function exists: libthink and
# the 62 DSP plugins stay free of it.
#
# It lands in <root>/plugins/visual/ alongside the DSP categories, so one
# THINK_PLUGIN_PATH still finds everything.
function(think_add_visual name)
  set(target "visual_${name}")

  add_library(${target} MODULE "visual/${name}.cpp")

  set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "${name}"
      PREFIX ""
      SUFFIX "${THINK_PLUGIN_SUFFIX}"
      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/visual")

  target_compile_definitions(${target} PRIVATE VISUAL_PLUGIN_BUILD)

  # src/ for thVisual.h, libthink/ for thExport.h and nothing else.
  target_include_directories(${target} PRIVATE
      "${PROJECT_SOURCE_DIR}"
      "${PROJECT_SOURCE_DIR}/src"
      "${PROJECT_SOURCE_DIR}/libthink")

  target_link_libraries(${target} PRIVATE PkgConfig::CAIRO m)

  set_target_properties(${target} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)

  install(TARGETS ${target}
          LIBRARY DESTINATION "${THINK_PKG_PLUGIN_DIR}/visual"
          RUNTIME DESTINATION "${THINK_PKG_PLUGIN_DIR}/visual")

  add_dependencies(plugins ${target})
endfunction()
