# The .gen composer in thinkbox: libthink, the host's scheduler and loader,
# every composer plugin and math/'s DSP plugins (all a piece's param
# expressions are built from), compiled in as the browser's build has them
# (cmake/ThinkStatic.cmake), with no instrument's graph: the BOX's voices
# are compiled. And sigc++, from source, the version the native build
# links, as wasm/cmake/ThinkEmscripten.cmake fetches it.
#
#   box_composer_sources(<out sources> <out include dirs>)

get_filename_component(BOX_TOP "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${BOX_TOP}/cmake/ThinkStatic.cmake")
include(FetchContent)

# The exports composer_draw aside: there is no cairo here (THC_NO_DRAW).
set(THINK_COMPOSER_EXPORTS init create tick receive param_changed input
                           capture destroy)
set(THINK_COMPOSER_PROTO_init          "int @ (thcComposerInfo *)")
set(THINK_COMPOSER_PROTO_create        "void *@ (const thcParams *)")
set(THINK_COMPOSER_PROTO_tick
    "double @ (void *, const thcTransport *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_receive
    "void @ (void *, const thcEvent *, thcEventSink *)")
set(THINK_COMPOSER_PROTO_param_changed "void @ (void *, int)")
set(THINK_COMPOSER_PROTO_input         "void @ (void *, const thcInputEvent *)")
set(THINK_COMPOSER_PROTO_capture       "const char *@ (void *, int)")
set(THINK_COMPOSER_PROTO_destroy       "void @ (void *)")

function(box_composer_sources out_sources out_includes)
  set(gen "${CMAKE_BINARY_DIR}/composer")

  set(PROJECT_NAME thinksynth)
  set(PROJECT_VERSION 0.1)
  set(THINK_DEFAULT_OUTPUT "")
  set(THINK_PLUGIN_SUFFIX "")
  set(THINK_PLUGIN_PATH "")
  set(THINK_DSP_PATH "")
  set(THINK_PATCH_PATH "")
  set(THINK_APP_ID "org.thinksynth.thinksynth")
  configure_file("${BOX_TOP}/cmake/config.h.in" "${gen}/config.h")

  FetchContent_Declare(sigc
      URL https://github.com/libsigcplusplus/libsigcplusplus/archive/refs/tags/3.6.0.tar.gz
      URL_HASH SHA256=bbe81e4f6d8acb41a9795525a38c0782751dbc4af3d78a9339f4a282e8a16c38
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_SUBDIR none)
  FetchContent_MakeAvailable(sigc)
  file(CONFIGURE OUTPUT "${gen}/sigc/sigc++config.h" CONTENT
"#ifndef SIGCXXCONFIG_H_INCLUDED
#define SIGCXXCONFIG_H_INCLUDED
#define SIGCXX_MAJOR_VERSION 3
#define SIGCXX_MINOR_VERSION 6
#define SIGCXX_MICRO_VERSION 0
#define SIGC_CONFIGURE 1
#define SIGC_API
#endif
")

  # At configure time: the IDF build has no flex rule. Copied only if it
  # changed, so a configure doesn't rebuild the lexer for nothing.
  find_program(BOX_FLEX flex REQUIRED)
  execute_process(COMMAND "${BOX_FLEX}" -o "${gen}/thinklex.new.cpp"
                          "${BOX_TOP}/libthink/thinklex.ll"
                  RESULT_VARIABLE rc)
  if(rc)
    message(FATAL_ERROR "flex failed on libthink/thinklex.ll")
  endif()
  file(COPY_FILE "${gen}/thinklex.new.cpp" "${gen}/thinklex.cpp" ONLY_IF_DIFFERENT)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${BOX_TOP}/libthink/thinklex.ll" "${BOX_TOP}/plugins/CMakeLists.txt")

  set(srcs "${gen}/thinklex.cpp")
  foreach(f thArg thArrays thCompiled thDspParser thExpr thChanEffect thMidiChan
            thMidiNote thMotion thSynthTree thNode thProbe thPlugin
            thMidiControllerConnection thMidiController thSoundFile thSynth
            thUtil thDynLib thPluginManager)
    list(APPEND srcs "${BOX_TOP}/libthink/${f}.cpp")
  endforeach()
  foreach(f thcPlugin thcScheduler thcAudition thcNodeHost thcGenFile)
    list(APPEND srcs "${BOX_TOP}/src/${f}.cpp")
  endforeach()
  if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND srcs "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/readlink.c")
  endif()

  # think_static_plugin reads its plugin from CMAKE_CURRENT_SOURCE_DIR, as
  # plugins/CMakeLists.txt calls it.
  set(CMAKE_CURRENT_SOURCE_DIR "${BOX_TOP}/plugins")
  set(PROJECT_BINARY_DIR "${gen}")
  file(READ "${BOX_TOP}/plugins/CMakeLists.txt" list)
  string(REGEX MATCH "think_add_plugin_category\\(math[^)]*\\)" math "${list}")
  string(REGEX REPLACE "^think_add_plugin_category\\(math|\\)$" "" math "${math}")
  string(REGEX REPLACE "[ \t\n]+" ";" math "${math}")
  foreach(name IN LISTS math)
    if(name)
      think_static_plugin(math "${name}")
    endif()
  endforeach()

  # A composer's exports, #defined to names of their own ahead of it, as
  # wasm/web/cmake/ThinkPlugin.cmake's think_add_composer does.
  string(REGEX MATCHALL "think_add_composer\\([a-z_0-9]+\\)" composers "${list}")
  foreach(c IN LISTS composers)
    string(REGEX REPLACE "think_add_composer\\(|\\)" "" c "${c}")
    set(defines "")
    foreach(ex IN LISTS THINK_COMPOSER_EXPORTS)
      string(APPEND defines "#define composer_${ex} thc_${c}_${ex}\n")
    endforeach()
    set(wrapper "${gen}/static/composer_${c}.cpp")
    file(CONFIGURE OUTPUT "${wrapper}" CONTENT
"#include \"thcstatic.h\"
${defines}
namespace thc_${c} {
#include \"${BOX_TOP}/plugins/composer/${c}.cpp\"
}
")
    set_property(GLOBAL APPEND PROPERTY THINK_STATIC_SOURCES "${wrapper}")
    set_property(GLOBAL APPEND PROPERTY THINK_STATIC_COMPOSERS "${c}")
  endforeach()

  think_static_registry("${gen}/static/registry.cpp")
  get_property(static GLOBAL PROPERTY THINK_STATIC_SOURCES)
  list(APPEND srcs ${static} "${gen}/static/registry.cpp")

  file(GLOB sigcsrcs "${sigc_SOURCE_DIR}/sigc++/*.cc"
                     "${sigc_SOURCE_DIR}/sigc++/functors/*.cc")
  list(APPEND srcs ${sigcsrcs})

  set(${out_sources} "${srcs}" PARENT_SCOPE)
  set(${out_includes} "${gen}" "${gen}/sigc" "${sigc_SOURCE_DIR}"
      "${BOX_TOP}" "${BOX_TOP}/wasm/web" "${BOX_TOP}/libthink" "${BOX_TOP}/src"
      "${BOX_TOP}/plugins" "${BOX_TOP}/plugins/composer" PARENT_SCOPE)
endfunction()
