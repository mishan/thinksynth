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
# Note what is deliberately absent: the plugins are not linked against
# libthink. They resolve thPlugin::regArg, thSynthTree::getArg and the rest
# against the host process at dlopen time, exactly as the autotools build did
# (configure.ac:180 links them with -lm and nothing else). That works on Linux
# and cannot work on Windows; PORTING.md section 4a is where it gets fixed,
# along with the THINK_API export macro it needs. Doing it here would make
# this build no longer comparable to the one it replaces.

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

  target_link_libraries(${target} PRIVATE PkgConfig::SIGC m)

  install(TARGETS ${target}
          LIBRARY DESTINATION
            "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/plugins/${category}")

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
