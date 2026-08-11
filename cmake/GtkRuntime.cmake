# The GTK runtime data files -- PORTING.md section 13.
#
# file(GET_RUNTIME_DEPENDENCIES) walks the *link* graph, so it collects DLLs
# and dylibs and stops there. GTK also needs three things that no linker ever
# mentions, and without which the closure produces a package that starts and
# then dies:
#
#   1. share/glib-2.0/schemas/gschemas.compiled
#      GTK reads org.gtk.Settings.FileChooser during init. If the schema is
#      not installed GSettings does not degrade, it aborts the process.
#
#   2. lib/gdk-pixbuf-2.0/<ver>/loaders{,.cache}
#      Every icon in the theme is a PNG or an SVG, and gdk-pixbuf loads
#      neither without a loader. See the long comment below -- this one is
#      not the same problem on both platforms.
#
#   3. share/icons/{Adwaita,hicolor}
#      GTK's default theme is Adwaita. This one is an improvement rather than
#      a rescue: GTK3 carries a fallback icon set compiled into its own
#      gresource, and `thinksynth -G' with the system themes hidden finds
#      image-missing, folder, document-open, list-add, go-up and edit-find
#      regardless. So the package renders without this -- just with GTK's
#      plainer built-ins in place of the theme's icons.
#
# What is deliberately NOT here is fonts. Pango picks its backend at runtime
# in pango_cairo_font_map_new(): CoreText where CoreText and Quartz are both
# available, then win32 where cairo has the Win32 surface, and only then
# fontconfig. macOS and Windows therefore both take a native backend that
# reads the fonts already installed on the machine, so there is nothing to
# ship -- no font files, and no etc/fonts/fonts.conf, since fontconfig is
# linked but never asked anything. That would change if anyone ever set
# PANGOCAIRO_BACKEND=fc, which nothing here does.

# Off on Linux, where the package is expected to use the distribution's GTK.
# Settable there anyway, and CI does set it: this is the only code in the
# tree that cannot otherwise be run on any machine we have.
if(APPLE OR WIN32)
  set(_think_bundle_gtk_default ON)
else()
  set(_think_bundle_gtk_default OFF)
endif()

option(THINK_BUNDLE_GTK
       "Ship GTK's schemas, pixbuf loaders and icon themes in the package"
       ${_think_bundle_gtk_default})

if(NOT THINK_BUNDLE_GTK)
  return()
endif()

# ---------------------------------------------------------------------------
# Where the GTK installation keeps each of them
#
# "The gtk+-3.0 prefix" is not one directory, and assuming it was is what
# broke macOS: Homebrew is keg-based, so pkg-config reports
# /opt/homebrew/Cellar/gtk+3/3.24.52, which holds gtk's own files and nothing
# else. The schemas are compiled into the *linked* prefix by gtk+3's
# post_install, the icon theme belongs to a different formula again, and
# librsvg's pixbuf loader to a third. On MSYS2 and on Linux all of it really
# is under one prefix, and that is what made the wrong assumption survive.
#
# So: a list of roots, searched in order, rather than a single prefix.
# ---------------------------------------------------------------------------

pkg_get_variable(_gtk_prefix       gtk+-3.0        prefix)
pkg_get_variable(_pixbuf_prefix    gdk-pixbuf-2.0  prefix)
pkg_get_variable(_pixbuf_moduledir gdk-pixbuf-2.0  gdk_pixbuf_moduledir)
pkg_get_variable(_pixbuf_cache     gdk-pixbuf-2.0  gdk_pixbuf_cache_file)
pkg_get_variable(_pixbuf_binver    gdk-pixbuf-2.0  gdk_pixbuf_binary_version)

if(NOT _gtk_prefix)
  message(FATAL_ERROR
      "THINK_BUNDLE_GTK is on but pkg-config cannot say where gtk+-3.0 is "
      "installed. Without that there is nothing to copy.")
endif()

set(THINK_GTK_DATA_ROOTS "" CACHE STRING
    "Directories to look for GTK's schemas, icon themes and pixbuf loaders \
under, replacing the ones worked out from pkg-config. For a GTK laid out in \
a way the guesses below do not cover.")

if(THINK_GTK_DATA_ROOTS)
  set(_roots ${THINK_GTK_DATA_ROOTS})
else()
  set(_roots "${_gtk_prefix}" "${_pixbuf_prefix}")

  # .../Cellar/<formula>/<version> -> the prefix its files are linked into.
  foreach(_p "${_gtk_prefix}" "${_pixbuf_prefix}")
    if(_p MATCHES "^(.*)/Cellar/")
      list(APPEND _roots "${CMAKE_MATCH_1}")
    endif()
  endforeach()

  if(DEFINED ENV{HOMEBREW_PREFIX})
    list(APPEND _roots "$ENV{HOMEBREW_PREFIX}")
  endif()

  list(APPEND _roots ${CMAKE_PREFIX_PATH})
endif()

list(REMOVE_DUPLICATES _roots)

message(STATUS "GTK data roots: ${_roots}")

# think_find_gtk_data(<out> <relative path>) -- first root that has it, or
# <out>-NOTFOUND. find_file() is not used because these are looked for
# relative to a specific set of prefixes, not on the system search path.
function(think_find_gtk_data out relative)
  foreach(_root IN LISTS _roots)
    if(EXISTS "${_root}/${relative}")
      set(${out} "${_root}/${relative}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out} "${out}-NOTFOUND" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# 1. The compiled schemas
#
# A prebuilt gschemas.compiled is preferred: it is a position-independent blob
# keyed by schema id, so it does not care where it ends up, and the one in the
# prefix already has every schema the installed GTK knows about.
#
# When there is none -- a keg that was never linked, a prefix whose
# post-install step has not run -- the XML is compiled here instead. That is
# not a fallback so much as the same thing done by hand, and it is worth
# having because the failure it avoids is an abort at startup rather than
# anything the build would notice.
# ---------------------------------------------------------------------------

# Everything found under a root may be a symlink rather than a file: Homebrew
# links each installed file from the prefix back into its keg. install(FILES)
# and install(DIRECTORY) both preserve symlinks, and those links are relative
# to the prefix, so copied into a bundle at another depth they dangle. So
# every source path is resolved to its real file before it is installed, and
# the directories are copied with `cmake -E copy_directory', which -- unlike
# install(DIRECTORY) -- dereferences as it goes.
#
# This is what made the macOS package ship an Adwaita whose icons could be
# listed but not opened.
function(think_real_path out path)
  get_filename_component(_real "${path}" REALPATH)
  set(${out} "${_real}" PARENT_SCOPE)
endfunction()

# think_install_tree(<source dir> <destination dir under the prefix>)
function(think_install_tree src dest)
  install(CODE "
    set(_dst \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${dest}\")
    message(STATUS \"Copying (dereferenced): ${src} -> \${_dst}\")
    execute_process(
        COMMAND \"${CMAKE_COMMAND}\" -E copy_directory \"${src}\" \"\${_dst}\"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR \"failed to copy ${src}\")
    endif()
  ")
endfunction()

think_find_gtk_data(_schemas "share/glib-2.0/schemas/gschemas.compiled")

if(_schemas)
  think_real_path(_schemas "${_schemas}")
endif()

if(_schemas)
  message(STATUS "GTK schemas: ${_schemas}")
  install(FILES "${_schemas}"
          DESTINATION "${THINK_PKG_GTK_DIR}/share/glib-2.0/schemas")
else()
  set(_schema_xml "")

  # *.enums.xml as well as *.gschema.xml. A schema that uses <enum> refers to
  # an id defined in a separate enums file, and glib-compile-schemas discards
  # the whole schema when it cannot resolve one -- silently enough that the
  # build still succeeds and the abort still happens at startup.
  foreach(_root IN LISTS _roots)
    file(GLOB _found "${_root}/share/glib-2.0/schemas/*.gschema.xml"
                     "${_root}/share/glib-2.0/schemas/*.enums.xml")
    list(APPEND _schema_xml ${_found})
  endforeach()

  if(NOT _schema_xml)
    message(FATAL_ERROR
        "No gschemas.compiled and no *.gschema.xml under any of ${_roots}. "
        "A package without them aborts on startup in g_settings_new(), so "
        "this is not a warning.")
  endif()

  find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas)

  if(NOT GLIB_COMPILE_SCHEMAS)
    message(FATAL_ERROR
        "Found schema XML but no glib-compile-schemas to compile it with.")
  endif()

  set(_schema_dir "${PROJECT_BINARY_DIR}/gtkruntime/schemas")

  file(MAKE_DIRECTORY "${_schema_dir}")
  file(COPY ${_schema_xml} DESTINATION "${_schema_dir}")

  execute_process(COMMAND "${GLIB_COMPILE_SCHEMAS}" "${_schema_dir}"
                  RESULT_VARIABLE _rc)

  if(NOT _rc EQUAL 0 OR NOT EXISTS "${_schema_dir}/gschemas.compiled")
    message(FATAL_ERROR "glib-compile-schemas failed on ${_schema_dir}")
  endif()

  list(LENGTH _schema_xml _n)
  message(STATUS "GTK schemas: compiled ${_n} XML file(s) from the kegs")

  install(FILES "${_schema_dir}/gschemas.compiled"
          DESTINATION "${THINK_PKG_GTK_DIR}/share/glib-2.0/schemas")
endif()

# ---------------------------------------------------------------------------
# 2. The gdk-pixbuf loaders
#
# A first pass skipped these entirely on Windows, because MSYS2 builds the
# shared gdk-pixbuf with -Dbuiltin_loaders=all. That is true and it is not
# sufficient: "all" is all the loaders in gdk-pixbuf's own source tree, and
# SVG is not one of them. It comes from librsvg, externally, on every
# platform -- and modern Adwaita is mostly SVG.
#
# A second pass copied the whole of gdk_pixbuf_moduledir. That works where a
# prefix is a directory and fails where it is not: under Homebrew the svg
# loader lives in librsvg's keg, not gdk-pixbuf's, so exactly the loader worth
# having is the one that would have been left behind.
#
# So the cache is the index, not the directory. Every module the cache names
# is installed by name, from wherever it happens to live, and the cache is
# rewritten to point at where they landed. That is one code path for all three
# platforms, and it drops the earlier dependence on gdk-pixbuf having been
# built -Drelocatable=true, since the rewritten paths are absolute and
# build_module_path() passes absolute paths through unchanged either way.
# ---------------------------------------------------------------------------

think_find_gtk_data(_cache_file
                    "lib/gdk-pixbuf-2.0/${_pixbuf_binver}/loaders.cache")

if(NOT _cache_file AND EXISTS "${_pixbuf_cache}")
  set(_cache_file "${_pixbuf_cache}")   # pkg-config's answer, if it is real
endif()

if(NOT _cache_file)
  message(WARNING
      "No gdk-pixbuf loaders.cache under any of ${_roots}. SVG icons will "
      "not render in the package.")
else()
  set(_pixbuf_dest "${THINK_PKG_GTK_DIR}/lib/gdk-pixbuf-2.0/${_pixbuf_binver}")

  # The separator in the *written* cache. Backslash on Windows, doubled
  # because the format C-escapes its quoted strings -- which is why
  # gdk-pixbuf-query-loaders emits "lib\\gdk-pixbuf-2.0\\..." there. The
  # substituted root is escaped to match, in gthGtkRuntime, so the path the
  # parser hands to g_module_open() is native from end to end rather than
  # half backslashes and half not.
  if(WIN32)
    set(_s "\\\\")
  else()
    set(_s "/")
  endif()

  set(_pixbuf_run
      "@THINK_BUNDLE_ROOT@${_s}lib${_s}gdk-pixbuf-2.0${_s}${_pixbuf_binver}${_s}loaders")

  file(READ "${_cache_file}" _cache_text)
  string(REPLACE ";" "\\;" _cache_text "${_cache_text}")
  string(REPLACE "\n" ";"  _cache_lines "${_cache_text}")

  set(_out "")
  set(_modules "")
  set(_bases "")

  foreach(_line IN LISTS _cache_lines)
    # A module line is a single quoted path alone on the line; every other
    # line in the format has either no quotes or several fields.
    if(_line MATCHES "^\"([^\"]+)\"[ \t\r]*$")
      set(_mod "${CMAKE_MATCH_1}")

      # The cache escapes backslashes, so a Windows path arrives doubled.
      string(REPLACE "\\\\" "/" _mod "${_mod}")

      if(NOT IS_ABSOLUTE "${_mod}")
        set(_mod "${_pixbuf_prefix}/${_mod}")
      endif()

      if(EXISTS "${_mod}")
        get_filename_component(_base "${_mod}" NAME)
        think_real_path(_mod "${_mod}")
        list(APPEND _modules "${_mod}")
        list(APPEND _bases "${_base}")
        list(APPEND _out "\"${_pixbuf_run}${_s}${_base}\"")
      else()
        # A stale cache naming a loader that is no longer installed. Dropping
        # the line rather than keeping it is deliberate: gdk-pixbuf treats a
        # module it cannot dlopen as a fatal parse error for the whole cache.
        message(STATUS "gdk-pixbuf: skipping missing loader ${_mod}")
        list(APPEND _out "")
      endif()
    else()
      list(APPEND _out "${_line}")
    endif()
  endforeach()

  list(LENGTH _modules _n)

  if(_n EQUAL 0)
    message(WARNING "loaders.cache at ${_cache_file} names no loaders that "
                    "exist. SVG icons will not render in the package.")
  else()
    message(STATUS "gdk-pixbuf: bundling ${_n} loader(s) named by ${_cache_file}")

    # One at a time, with an explicit RENAME, because the name in the cache
    # and the name of the file on disk are not required to agree. _base is
    # taken from the path the cache gave -- which under Homebrew is a link in
    # the prefix -- while the file actually installed is what REALPATH
    # resolved to, in some keg, under whatever name that keg chose. Installing
    # the resolved file under its own basename and then naming the other one
    # in the cache produces exactly the failure macOS reported: a cache entry
    # pointing at a file that is not there.
    #
    # RENAME takes a single file, so this cannot be one install(FILES).
    list(LENGTH _modules _count)
    math(EXPR _last "${_count} - 1")

    foreach(_i RANGE ${_last})
      list(GET _modules "${_i}" _m)
      list(GET _bases   "${_i}" _b)

      install(FILES "${_m}" DESTINATION "${_pixbuf_dest}/loaders" RENAME "${_b}")
    endforeach()

    # THINK_BUNDLE_ROOT is deliberately not a valid path, so a cache that
    # somehow escapes substitution fails to load rather than silently picking
    # up whatever the build machine happened to have.
    string(REPLACE ";" "\n" _out_text "${_out}")
    string(REPLACE "\\;" ";" _out_text "${_out_text}")

    set(_staged_cache "${PROJECT_BINARY_DIR}/gtkruntime/loaders.cache.in")
    file(WRITE "${_staged_cache}" "${_out_text}")

    install(FILES "${_staged_cache}" DESTINATION "${_pixbuf_dest}")

    # The loaders' own dependencies.
    #
    # This is the same hole as the one that started all of this, one level
    # further down. file(GET_RUNTIME_DEPENDENCIES) walks the link graph of the
    # targets it is given, and a pixbuf loader is in nobody's link graph -- it
    # is dlopen'd by gdk-pixbuf at run time. So the .app got
    # libpixbufloader_svg.so and not librsvg, and dlopen failed on
    # @rpath/librsvg-2.2.dylib. The tiff loader would have gone the same way.
    #
    # MODULES is the argument for exactly this: things loaded at run time
    # rather than linked. The excludes and the search directories are the ones
    # cmake/Packaging.cmake already worked out.
    if(THINK_PKG_DEPS)
      # Embedded as pre-quoted arguments because these are regexes and paths,
      # and neither survives being pasted in bare.
      set(_q_mods "")
      set(_q_pre  "")
      set(_q_post "")
      set(_q_dirs "")

      foreach(_x IN LISTS _modules)
        string(APPEND _q_mods " \"${_x}\"")
      endforeach()
      foreach(_x IN LISTS THINK_DEP_PRE_EXCLUDE)
        string(APPEND _q_pre " \"${_x}\"")
      endforeach()
      foreach(_x IN LISTS THINK_DEP_POST_EXCLUDE)
        string(APPEND _q_post " \"${_x}\"")
      endforeach()
      foreach(_x IN LISTS THINK_DEP_DIRS)
        string(APPEND _q_dirs " \"${_x}\"")
      endforeach()

      install(CODE "
        file(GET_RUNTIME_DEPENDENCIES
             MODULES${_q_mods}
             RESOLVED_DEPENDENCIES_VAR   _res
             UNRESOLVED_DEPENDENCIES_VAR _unres
             PRE_EXCLUDE_REGEXES${_q_pre}
             POST_EXCLUDE_REGEXES${_q_post}
             DIRECTORIES${_q_dirs})

        foreach(_f IN LISTS _res)
          file(INSTALL
               DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${THINK_PKG_LIB_DIR}\"
               TYPE SHARED_LIBRARY
               FOLLOW_SYMLINK_CHAIN
               FILES \"\${_f}\")
        endforeach()

        # Reported rather than fatal: an unresolved @rpath entry is not
        # always a missing library, and `thinksynth -G' is the check with
        # teeth -- it dlopens a loader for real. But it is printed, because
        # this list is where the next one of these will show up first.
        if(_unres)
          message(WARNING
              \"pixbuf loaders have dependencies that could not be resolved, \"
              \"so the package may not load images: \${_unres}\")
        endif()
      ")
    endif()

    # And then check the install did what it was told.
    #
    # A loader named by the cache but absent from the package is invisible
    # until someone tries to decode an image, and then it is a dlopen failure
    # from inside GTK with no hint as to which build step lost it. macOS
    # produced exactly that -- a cache naming libpixbufloader_svg.so and no
    # such file in the bundle -- and reading the code above did not explain
    # it. Checking here turns that into a failed install naming the file.
    install(CODE "
      set(_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${_pixbuf_dest}/loaders\")
      set(_missing \"\")
      foreach(_b ${_bases})
        if(NOT EXISTS \"\${_dir}/\${_b}\")
          list(APPEND _missing \"\${_b}\")
        endif()
      endforeach()
      if(_missing)
        file(GLOB _present \"\${_dir}/*\")
        message(FATAL_ERROR
            \"loaders.cache names modules that were not installed.\n\"
            \"  missing:   \${_missing}\n\"
            \"  directory: \${_dir}\n\"
            \"  contains:  \${_present}\")
      endif()
    ")
  endif()
endif()

# ---------------------------------------------------------------------------
# 2b. The shared MIME database
#
# Not on the original list of three, and it should have been. gdk-pixbuf
# identifies a file by sniffing its first bytes against each loader's magic,
# and for SVG -- which is XML, and so has no distinctive leading bytes -- that
# is inconclusive. It falls back to g_content_type_guess(), which reads the
# freedesktop MIME database, which GLib finds through XDG_DATA_DIRS.
#
# With no MIME database reachable, g_content_type_guess() returns
# application/octet-stream and gdk-pixbuf reports "Couldn't recognize the
# image file format" for a perfectly good SVG. On Linux the system copy is
# always there; on macOS and Windows there is nothing outside the bundle, so
# the bundle has to carry it.
#
# Only the top-level index files. The per-type subdirectories are XML
# descriptions for user-facing labels -- around 7 MB of them -- and nothing
# here asks for a description.
# ---------------------------------------------------------------------------

think_find_gtk_data(_mime_dir "share/mime/mime.cache")

if(_mime_dir)
  get_filename_component(_mime_dir "${_mime_dir}" DIRECTORY)

  # By name rather than by glob: a glob would have to distinguish files from
  # the per-type subdirectories, and the first attempt did that by testing for
  # a dot in the name -- which kept mime.cache and silently dropped globs2,
  # magic, aliases and the rest, the fallbacks GLib uses when there is no
  # mime.cache to read.
  set(_mime_files "")

  foreach(_f mime.cache globs globs2 magic aliases subclasses icons
             generic-icons types treemagic XMLnamespaces version)
    if(EXISTS "${_mime_dir}/${_f}")
      think_real_path(_real "${_mime_dir}/${_f}")
      list(APPEND _mime_files "${_real}")
    endif()
  endforeach()

  message(STATUS "MIME database: ${_mime_dir}")

  install(FILES ${_mime_files}
          DESTINATION "${THINK_PKG_GTK_DIR}/share/mime")
else()
  message(WARNING
      "No share/mime/mime.cache under any of ${_roots}. SVG icons will not "
      "be recognised in the package, because gdk-pixbuf identifies XML "
      "formats by MIME type rather than by magic.")
endif()

# ---------------------------------------------------------------------------
# 3. The icon themes
#
# Whole themes, not a hand-picked subset. Picking out "the icons this app
# uses" is not something that can be determined from the source -- GTK asks
# for icons by name from inside the file chooser, the menus and the window
# controls -- and getting the list wrong shows up as a blank button on someone
# else's machine rather than as a build failure.
#
# The cost is real: Adwaita is about 5 MB from a GTK prefix. On Linux, where
# this is only ever switched on to test the mechanism, share/icons is the
# whole system's worth of application icons and comes to tens of megabytes.
# That is a testing artefact and not what macOS or Windows will ship.
#
# icon-theme.cache is not generated. It is a lookup optimisation; GTK falls
# back to scanning the directory when it is missing or stale, which costs
# some stat() calls at startup and nothing else.
# ---------------------------------------------------------------------------

set(_have_theme FALSE)

# AdwaitaLegacy carries the fixed-size PNGs. It is a hard dependency of the
# Adwaita package on MSYS2 and absent elsewhere, so it is looked for rather
# than required.
foreach(_theme Adwaita AdwaitaLegacy hicolor)
  think_find_gtk_data(_theme_dir "share/icons/${_theme}")

  if(_theme_dir)
    think_install_tree("${_theme_dir}"
                       "${THINK_PKG_GTK_DIR}/share/icons/${_theme}")

    if(NOT _theme STREQUAL "AdwaitaLegacy")
      set(_have_theme TRUE)
    endif()
  endif()
endforeach()

if(NOT _have_theme)
  message(WARNING
      "No Adwaita or hicolor icon theme under any of ${_roots}. The package "
      "will fall back to the icons compiled into GTK, which is survivable "
      "but plain.")
endif()
