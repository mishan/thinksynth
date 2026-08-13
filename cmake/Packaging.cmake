# Packaging -- see PACKAGING.md. The layout itself is decided in
# cmake/Layout.cmake, which has to run before the subdirectories so their
# install() calls can use it; this half needs the targets to exist, so it runs
# last.

# ---------------------------------------------------------------------------
# Where the loader looks for libthink
# ---------------------------------------------------------------------------

if(APPLE)
  # A bundle is relocatable by definition -- it gets dragged wherever the user
  # likes -- so nothing absolute may appear in the load commands.
  set_target_properties(thinksynth PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "thinksynth"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${THINK_APP_ID}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      INSTALL_RPATH "@executable_path/../Frameworks")

  set_target_properties(think PROPERTIES
      INSTALL_NAME_DIR "@rpath")
elseif(WIN32)
  # Windows has no rpath; a DLL is found next to the exe, which is where the
  # packaging puts it.
elseif(UNIX)
  # $ORIGIN so an unpacked tarball works from wherever it was unpacked, with
  # the configured libdir kept as a fallback for a real system install.
  set_target_properties(thinksynth PROPERTIES
      INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR};${CMAKE_INSTALL_FULL_LIBDIR}")
endif()

# ---------------------------------------------------------------------------
# The dependency closure
#
# gtkmm-3 drags roughly forty libraries behind it, and on Windows and macOS
# there is no system package manager to have put them there. CMake can walk
# the closure itself since 3.21; on anything older the packaging still works
# but produces something that only runs where it was built, so say so rather
# than shipping a bundle that fails on someone else's machine.
# ---------------------------------------------------------------------------

if(APPLE OR WIN32)
  set(_think_deps_default ON)
else()
  # Linux packages are expected to depend on the distribution's own GTK.
  set(_think_deps_default OFF)
endif()

# An option rather than a fact about the platform, for the same reason
# THINK_BUNDLE_GTK is: it is otherwise code that only ever runs where it
# cannot be watched. Turning it on for a Linux build produces a package
# carrying the GTK stack, which is not what a Linux package should be, but is
# the only way to see the closure work before macOS or Windows sees it.
option(THINK_PKG_DEPS
       "Collect the runtime dependency closure into the package"
       ${_think_deps_default})

if(THINK_PKG_DEPS AND CMAKE_VERSION VERSION_LESS 3.21)
  message(WARNING
      "CMake ${CMAKE_VERSION} cannot collect runtime dependencies "
      "(3.21+ needed). The package will build but will only run on a machine "
      "that already has the GTK stack installed.")
  set(THINK_PKG_DEPS OFF)
endif()

# The application itself, always. This used to sit inside the
# if(THINK_PKG_DEPS) below, so on a macOS or Windows machine with CMake older
# than 3.21 -- where dependency collection is unavailable -- the package came
# out with no thinksynth in it at all, src/CMakeLists.txt having stopped
# installing it on those platforms. Only the closure is conditional.
if(APPLE OR WIN32)
  if(THINK_PKG_DEPS)
    install(TARGETS thinksynth
            RUNTIME_DEPENDENCY_SET thinkDeps
            BUNDLE  DESTINATION "."
            RUNTIME DESTINATION ".")
  else()
    install(TARGETS thinksynth
            BUNDLE  DESTINATION "."
            RUNTIME DESTINATION ".")
  endif()
endif()

if(THINK_PKG_DEPS)
  # Windows hands back "C:\Windows\system32/kernel32.dll" -- both separators
  # in one path -- and under CMP0207's old behaviour the exclude regexes are
  # matched against exactly that. Ours happen to match either spelling, so
  # this is about determinism and about five screens of warnings, not about a
  # bug we have. NEW normalizes first.
  if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
  endif()

  # What NOT to bundle: the operating system's own libraries. Shipping a copy
  # of the C runtime or of a system framework is at best redundant and at
  # worst the cause of two incompatible copies being loaded at once.
  if(WIN32)
    # file(GET_RUNTIME_DEPENDENCIES) does not search PATH on Windows. That is
    # deliberate on CMake's part -- a package should not be a function of
    # whatever the build shell happened to have exported -- so DIRECTORIES is
    # the only place it looks. It was ${CMAKE_PREFIX_PATH}, which nothing in
    # this build or in CI ever sets: an empty search path, and every one of
    # the eleven MinGW and GTK DLLs came back unresolved.
    #
    # Under MSYS2 the whole UCRT64 prefix is one bin directory -- the C++
    # runtime, libwinpthread, and the entire gtkmm stack alike -- and it is
    # the directory holding the compiler. That single entry covers the whole
    # list cpack complained about.
    get_filename_component(_think_dep_dirs "${CMAKE_CXX_COMPILER}" DIRECTORY)

    # For a MinGW that is not MSYS2, where GTK may live under its own prefix.
    # Harmless when it duplicates the above or does not exist.
    pkg_get_variable(_think_gtkmm_prefix gtkmm-3.0 prefix)
    if(_think_gtkmm_prefix)
      list(APPEND _think_dep_dirs "${_think_gtkmm_prefix}/bin")
    endif()

    # And an escape hatch that now actually means something.
    foreach(_p IN LISTS CMAKE_PREFIX_PATH)
      list(APPEND _think_dep_dirs "${_p}/bin" "${_p}")
    endforeach()

    list(REMOVE_DUPLICATES _think_dep_dirs)

    # Printed because the failure mode is silent: an unresolved DLL names the
    # DLL but never says where it looked.
    message(STATUS "Runtime dependency search path: ${_think_dep_dirs}")

    set(THINK_DEP_PRE_EXCLUDE
        "api-ms-win-.*" "ext-ms-.*" "^hvsifiletrust\\.dll$"
        "^pdmutilities\\.dll$")
    set(THINK_DEP_POST_EXCLUDE ".*[Ss]ystem32.*")
    set(THINK_DEP_DIRS ${_think_dep_dirs})
  elseif(APPLE)
    set(THINK_DEP_PRE_EXCLUDE
        "^/usr/lib/libSystem" "^/usr/lib/libc\\+\\+" "^/System/Library/")
    set(THINK_DEP_POST_EXCLUDE "^/usr/lib/" "^/System/Library/")
    set(THINK_DEP_DIRS "")
  else()
    # Linux, which only happens when THINK_PKG_DEPS was asked for explicitly.
    # The C library and the compiler runtime are excluded by name; everything
    # else is fair game, since collecting the GTK stack is the entire point of
    # having switched this on here.
    set(THINK_DEP_PRE_EXCLUDE
        "^ld-linux.*" "^libc\\.so" "^libm\\.so" "^libdl\\.so"
        "^librt\\.so" "^libpthread\\.so" "^libstdc\\+\\+\\.so"
        "^libgcc_s\\.so")
    set(THINK_DEP_POST_EXCLUDE "")
    set(THINK_DEP_DIRS "")
  endif()

  # Named rather than written twice: cmake/GtkRuntime.cmake needs the same
  # three lists to close over the pixbuf loaders, which are dlopen'd and so
  # are not in any target's link graph.
  install(RUNTIME_DEPENDENCY_SET thinkDeps
          PRE_EXCLUDE_REGEXES  ${THINK_DEP_PRE_EXCLUDE}
          POST_EXCLUDE_REGEXES ${THINK_DEP_POST_EXCLUDE}
          DIRECTORIES          ${THINK_DEP_DIRS}
          DESTINATION "${THINK_PKG_LIB_DIR}")
endif()

# ---------------------------------------------------------------------------
# GTK's own data files, which the closure above cannot find because nothing
# links against them. Kept in its own file: it is longer than everything here
# put together, and the reasoning does not fit in a comment.
# ---------------------------------------------------------------------------

include(GtkRuntime)

# ---------------------------------------------------------------------------
# Ad-hoc code signature, macOS only, and it has to be last
#
# "thinksynth.app is damaged and can't be opened" is what a tester got from
# the arm64 .dmg, and the app was not damaged: it was unsigned in the one way
# Apple Silicon does not tolerate.
#
# Every Mach-O the linker produces on arm64 is ad-hoc signed by the linker
# itself, because the kernel refuses to execute an arm64 binary with no
# signature at all. But the signature covers the file as it was linked, and
# this build modifies every one of them afterwards: CMAKE_BUILD_WITH_INSTALL_RPATH
# is OFF, so install_name_tool rewrites the rpath of the executable and the
# install name of libthink at *install* time, and file(GET_RUNTIME_DEPENDENCIES)
# copies forty-odd dylibs in. A binary whose load commands changed after it was
# signed has a signature that no longer matches its contents, which is
# precisely what "damaged" means here.
#
# So: re-sign, after every other install rule has run. Nested code first and
# the bundle last, because signing a bundle seals what is inside it and
# anything signed afterwards invalidates the seal.
#
# This does NOT make the download open on someone else's Mac by itself -- see
# PACKAGING.md. An ad-hoc signature has no Developer ID behind it, and a
# quarantined app without one is still refused. It is the difference between a
# package that cannot work and a package that needs the quarantine flag
# cleared, which is a thing a user can actually do.
# ---------------------------------------------------------------------------

# Beside the .app in the .dmg, so the person who hits the Gatekeeper dialog
# has the answer in the window they are already looking at rather than in a
# repository they have never seen.
if(APPLE)
  install(FILES "${PROJECT_SOURCE_DIR}/data/README-macOS.txt" DESTINATION ".")
endif()

if(APPLE)
  install(CODE "
    set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/thinksynth.app\")

    if(NOT IS_DIRECTORY \"\${_app}\")
      message(FATAL_ERROR \"no bundle at \${_app} to sign\")
    endif()

    # Everything with code in it that is not the bundle itself.
    file(GLOB_RECURSE _nested
         \"\${_app}/Contents/Frameworks/*.dylib\"
         \"\${_app}/Contents/Resources/plugins/*${THINK_PLUGIN_SUFFIX}\")

    set(_signed 0)

    foreach(_f IN LISTS _nested)
      # Skip the version symlinks a dylib install leaves behind: codesign
      # follows them and would sign the same file several times, and the
      # second attempt on an already-signed file is an error rather than a
      # no-op.
      if(NOT IS_SYMLINK \"\${_f}\")
        execute_process(
            COMMAND codesign --force --sign - --timestamp=none \"\${_f}\"
            RESULT_VARIABLE _rc ERROR_VARIABLE _err)

        if(NOT _rc EQUAL 0)
          message(FATAL_ERROR \"codesign failed on \${_f}: \${_err}\")
        endif()

        math(EXPR _signed \"\${_signed} + 1\")
      endif()
    endforeach()

    execute_process(
        COMMAND codesign --force --sign - --timestamp=none \"\${_app}\"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR \"codesign failed on the bundle: \${_err}\")
    endif()

    # Checked rather than assumed. --strict makes codesign apply the same
    # rules the loader will, and --deep walks the nested code, so a bundle
    # that would be refused at launch is refused here instead -- while
    # somebody is still watching the build.
    execute_process(
        COMMAND codesign --verify --strict --deep --verbose=2 \"\${_app}\"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR
          \"the signed bundle does not verify, so it would not launch:\n\${_err}\")
    endif()

    message(STATUS
        \"ad-hoc signed \${_signed} nested binaries and the bundle; verified\")
  ")
endif()

# ---------------------------------------------------------------------------
# CPack
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Metaphonic Labs")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A modular software synthesizer")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_FILE_NAME
    "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# Nothing here produces an installer that writes outside its own directory.
# That is deliberate: an unpacked directory that runs is worth more right now
# than a .pkg or an MSI nobody has tested, and the search paths make it work.
if(APPLE)
  set(CPACK_GENERATOR "DragNDrop;TGZ")
  set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME} ${PROJECT_VERSION}")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
