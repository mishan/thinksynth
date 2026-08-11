# Packaging -- PORTING.md section 8 step 8.
#
# Three shapes, one rule: the binary has to find its plugins, DSPs and patches
# without anything being configured, because on macOS and Windows there is no
# install prefix to point at.
#
#   Linux    <prefix>/bin/thinksynth
#            <prefix>/lib/thinksynth/plugins/<category>/
#            <prefix>/share/thinksynth/{dsp,patches}/
#
#   macOS    thinksynth.app/Contents/MacOS/thinksynth
#            thinksynth.app/Contents/Resources/plugins/<category>/
#            thinksynth.app/Contents/Resources/{dsp,patches}/
#            thinksynth.app/Contents/Frameworks/libthink.dylib + the gtkmm set
#
#   Windows  thinksynth/thinksynth.exe
#            thinksynth/plugins/<category>/
#            thinksynth/{dsp,patches}/
#            thinksynth/*.dll -- libthink and the MinGW/GTK closure
#
# Those are not invented here. thPluginManager::resolveRoot and
# thUtil::findDataFile already search <exe>/../Resources/<kind>, <exe>/<kind>
# and <exe>/../share/thinksynth/<kind>, in that spirit; this file arranges the
# files so that those searches hit. That is the part which can be checked
# without a Mac or a Windows box, and scripts/../ci does check it.

if(APPLE)
  set(THINK_PKG_PLUGIN_DIR "thinksynth.app/Contents/Resources/plugins")
  set(THINK_PKG_DATA_DIR   "thinksynth.app/Contents/Resources")
  set(THINK_PKG_LIB_DIR    "thinksynth.app/Contents/Frameworks")
elseif(WIN32)
  set(THINK_PKG_PLUGIN_DIR "plugins")
  set(THINK_PKG_DATA_DIR   ".")
  set(THINK_PKG_LIB_DIR    ".")
else()
  set(THINK_PKG_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/plugins")
  set(THINK_PKG_DATA_DIR   "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
  set(THINK_PKG_LIB_DIR    "${CMAKE_INSTALL_LIBDIR}")
endif()

# GTK's own data -- schemas, pixbuf loaders, icon themes -- when the package
# ships it. See cmake/GtkRuntime.cmake for what goes in and why.
#
# It gets a name of its own rather than being spelled THINK_PKG_DATA_DIR at
# every use, because unlike plugins and DSPs this is not found by our code:
# GTK finds it, via environment variables gthGtkRuntime sets, and those want
# one prefix-shaped directory with share/ and lib/ underneath it.
#
# It resolves to the same root the DSP search already uses, so the candidates
# gthGtkRuntime tries at runtime are ones thUtil::findDataFile already tries:
# <exe>/../Resources on macOS, <exe> on Windows, <exe>/../share/thinksynth on
# Linux.
set(THINK_PKG_GTK_DIR "${THINK_PKG_DATA_DIR}")

