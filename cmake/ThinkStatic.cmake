# Plugins compiled into the binary instead of dlopened.
#
# Two builds have nothing to dlopen with: the browser's AudioWorklet, which
# has no loader and no file system, and think_embedded, which a host such as
# an audio plugin links whole because it cannot count on a plugins/
# directory beside it. In both, every plugin is compiled into the one
# binary, each inside a namespace of its own -- every plugin defines the
# same handful of names, and one binary can hold only one of each -- and
# listed in the table thDynLib's static branch looks names up in
# (THINK_STATIC_PLUGINS, libthink/thDynLib.h). The plugin list is still
# plugins/CMakeLists.txt's, read as it stands.
#
# What a plugin contributes is a wrapper source and a row in the table.
# think_static_plugin writes the first and records the second in a global
# property; think_static_registry writes the table from the properties. The
# browser build also records composers and visuals the same way
# (wasm/web/cmake/ThinkPlugin.cmake), with the export lists and prototypes
# its CMakeLists.txt sets.

# A global property, because the table is written from whichever directory
# makes the binary, and a variable set here is out of scope there.
get_filename_component(prelude
    "${CMAKE_CURRENT_LIST_DIR}/../plugins/thinkstatic.h" ABSOLUTE)
set_property(GLOBAL PROPERTY THINK_STATIC_PRELUDE "${prelude}")

# think_static_plugin(<category> <name>)
#
# A wrapper source, and a name to find its row by. Called from
# plugins/CMakeLists.txt, so CMAKE_CURRENT_SOURCE_DIR is plugins/.
#
# The DSP plugins' exports are plain C++ names once PLUGIN_BUILD is left
# undefined, so a namespace is the whole trick.
function(think_static_plugin category name)
  get_property(THINK_STATIC_PRELUDE GLOBAL PROPERTY THINK_STATIC_PRELUDE)
  set(id "${category}_${name}")
  set(wrapper "${PROJECT_BINARY_DIR}/static/${id}.cpp")

  file(CONFIGURE OUTPUT "${wrapper}" CONTENT
"/* plugins/${category}/${name}.cpp, in a namespace of its own. Written by
   cmake/ThinkStatic.cmake; see plugins/thinkstatic.h. */
#include \"${THINK_STATIC_PRELUDE}\"

namespace thp_${id} {
#include \"${CMAKE_CURRENT_SOURCE_DIR}/${category}/${name}.cpp\"
}
")

  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_SOURCES "${wrapper}")
  set_property(GLOBAL APPEND PROPERTY THINK_STATIC_PLUGINS "${category}/${name}")

  # module_reset is optional, and the table can only point at one the
  # plugin defines: a row naming a function that does not exist fails the
  # link. A weak declaration would let the linker decide, as it does for
  # the composers, but a weak reference to an undefined C++ function is
  # not a thing MinGW does reliably. So the source is read for it instead.
  # A definition this misses leaves the row out, which is what
  # dsphash.embedded's second pass is there to notice.
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${category}/${name}.cpp" reset
       REGEX "^[ \t]*void[ \t]+module_reset[ \t]*\\(")

  if(reset)
    set_property(GLOBAL APPEND PROPERTY THINK_STATIC_RESETS "${category}/${name}")
  endif()
endfunction()

# think_static_registry(<output>)
#
# The table: one row per plugin recorded above, naming the functions its
# namespace defines and the version byte its ABI is gated on. A plugin
# whose functions do not match these declarations fails at the link, which
# is the right place.
#
# Composers and visuals are in it only where something recorded them, and
# the headers their rows need are included only then: a native build of the
# DSP plugins alone has no src/ on its include path and no thVisual.h.
function(think_static_registry output)
  get_property(THINK_STATIC_PRELUDE GLOBAL PROPERTY THINK_STATIC_PRELUDE)
  get_property(STATIC_PLUGINS   GLOBAL PROPERTY THINK_STATIC_PLUGINS)
  get_property(STATIC_RESETS    GLOBAL PROPERTY THINK_STATIC_RESETS)
  get_property(STATIC_COMPOSERS GLOBAL PROPERTY THINK_STATIC_COMPOSERS)
  get_property(STATIC_VISUALS   GLOBAL PROPERTY THINK_STATIC_VISUALS)

  set(includes "")
  set(versions "unsigned char dspVersion_ = MODULE_IFACE_VER;\n")
  set(decls "")
  set(syms "")
  set(rows "")
  set(composer_names "")
  set(visual_names "")

  if(STATIC_COMPOSERS)
    string(APPEND includes "#include \"thcomposer.h\"\n")
    string(APPEND versions
           "unsigned char composerVersion_ = COMPOSER_IFACE_VER;\n")
  endif()

  if(STATIC_VISUALS)
    string(APPEND includes "#include \"thVisual.h\"\n")
    string(APPEND versions
           "unsigned char visualVersion_ = VISUAL_IFACE_VER;\n")
  endif()

  foreach(p IN LISTS STATIC_PLUGINS)
    string(REPLACE "/" "_" id "${p}")

    set(reset_decl "")
    set(reset_row "")

    if(p IN_LIST STATIC_RESETS)
      set(reset_decl "    void module_reset (thPlugin *);\n")
      set(reset_row
          "    { \"module_reset\",    (void *)&thp_${id}::module_reset },\n")
    endif()

    string(APPEND decls
"namespace thp_${id} {
    int  module_init (thPlugin *);
    int  module_callback (thNode *, thSynthTree *, unsigned int, unsigned int);
    void module_cleanup (thPlugin *);
${reset_decl}}
")
    string(APPEND syms
"const thStaticSymbol dsp_${id}[] = {
    { \"apiversion\",      &dspVersion_ },
    { \"module_init\",     (void *)&thp_${id}::module_init },
    { \"module_callback\", (void *)&thp_${id}::module_callback },
    { \"module_cleanup\",  (void *)&thp_${id}::module_cleanup },
${reset_row}};
")
    string(APPEND rows "    TH_ROW(\"${p}\", dsp_${id}),\n")
  endforeach()

  # Every export a composer may have is declared for every composer, weak:
  # the address of one the plugin did not define links as a null, and a null
  # in the table is what thcPlugin reads as "not offered" -- the same answer
  # dlopen gives for a module without the symbol. So which exports a
  # composer has is decided by what it compiled, not by a scan of how it
  # spelled them.
  foreach(c IN LISTS STATIC_COMPOSERS)
    string(APPEND decls "extern \"C\" {\n")
    string(APPEND syms
"const thStaticSymbol com_${c}[] = {
    { \"composer_apiversion\", &composerVersion_ },
")

    foreach(ex IN LISTS THINK_COMPOSER_EXPORTS)
      string(REPLACE "@" "thc_${c}_${ex}" proto "${THINK_COMPOSER_PROTO_${ex}}")
      string(APPEND decls "    ${proto} __attribute__((weak));\n")
      string(APPEND syms
             "    { \"composer_${ex}\", (void *)&thc_${c}_${ex} },\n")
    endforeach()

    string(APPEND decls "}\n")
    string(APPEND syms "};\n")
    string(APPEND rows "    TH_ROW(\"composer/${c}\", com_${c}),\n")
    string(APPEND composer_names "    \"composer/${c}\",\n")
  endforeach()

  # The visuals, exactly as the composers: every export declared weak for
  # every module, so what the module compiled decides which it has.
  foreach(v IN LISTS STATIC_VISUALS)
    string(APPEND decls "extern \"C\" {\n")
    string(APPEND syms
"const thStaticSymbol vis_${v}[] = {
    { \"visual_apiversion\", &visualVersion_ },
")

    foreach(ex IN LISTS THINK_VISUAL_EXPORTS)
      string(REPLACE "@" "thv_${v}_${ex}" proto "${THINK_VISUAL_PROTO_${ex}}")
      string(APPEND decls "    ${proto} __attribute__((weak));\n")
      string(APPEND syms
             "    { \"visual_${ex}\", (void *)&thv_${v}_${ex} },\n")
    endforeach()

    string(APPEND decls "}\n")
    string(APPEND syms "};\n")
    string(APPEND rows "    TH_ROW(\"visual/${v}\", vis_${v}),\n")
    string(APPEND visual_names "    \"visual/${v}\",\n")
  endforeach()

  # The name lists end in a NULL, so a build with no composers or no
  # visuals still has an array to declare; the counts leave it out.
  file(CONFIGURE OUTPUT "${output}" CONTENT
"/* The table thDynLib's static branch reads: every plugin recorded by
   cmake/ThinkStatic.cmake. Written by think_static_registry. */
#include \"${THINK_STATIC_PRELUDE}\"
${includes}#include \"thDynLib.h\"

namespace {

/* What thPlugin, thcPlugin and thVisual check a plugin's version byte
   against before they call anything in it. A plugin compiled into this
   binary was compiled against these headers, so these are its. */
${versions}
} /* namespace */

${decls}
namespace {

${syms}
#define TH_ROW(name, syms) { name, syms, sizeof(syms) / sizeof(syms[0]) }

} /* namespace */

const thStaticPlugin thStaticPlugins[] = {
${rows}};

const size_t thStaticPluginCount =
    sizeof(thStaticPlugins) / sizeof(thStaticPlugins[0]);

const char *const thStaticComposers[] = {
${composer_names}    NULL
};

const size_t thStaticComposerCount =
    sizeof(thStaticComposers) / sizeof(thStaticComposers[0]) - 1;

const char *const thStaticVisuals[] = {
${visual_names}    NULL
};

const size_t thStaticVisualCount =
    sizeof(thStaticVisuals) / sizeof(thStaticVisuals[0]) - 1;
")
endfunction()
