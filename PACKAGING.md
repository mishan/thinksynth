# Packaging

Three layouts, one requirement: the binary finds its plugins, DSPs and patches
with nothing configured, because on macOS and Windows there is no install prefix
to point at.

```
Linux    <prefix>/bin/thinksynth
         <prefix>/lib/thinksynth/plugins/<category>/
         <prefix>/share/thinksynth/{dsp,patches}/

macOS    thinksynth.app/Contents/MacOS/thinksynth
         thinksynth.app/Contents/Resources/{plugins,dsp,patches}/
         thinksynth.app/Contents/Frameworks/libthink.dylib + the gtkmm set

Windows  thinksynth/thinksynth.exe
         thinksynth/{plugins,dsp,patches}/
         thinksynth/*.dll -- libthink and the MinGW/GTK closure
```

Those are not new inventions. `thPluginManager::resolveRoot` and
`thUtil::findDataFile` already search `<exe>/../Resources/<kind>`,
`<exe>/<kind>` and `<exe>/../share/thinksynth/<kind>`; `cmake/Layout.cmake` just
arranges the files so those searches hit.

**The layouts were checked on Linux before any other machine had run them**, and
the technique is worth keeping. The search paths are platform-independent code,
so a bundle-shaped tree can be built by hand and exercised: `dspcheck` placed at
`Contents/MacOS/`, plugins at `Contents/Resources/plugins`, DSPs at
`Contents/Resources/dsp`, run from `/tmp` with no environment set and no system
install — the whole loadable patch corpus came up. The same for the Windows
shape, everything beside the executable. And the Linux tarball unpacks anywhere
and runs: `$ORIGIN/../lib` resolves libthink, and the plugins, DSPs and patches
are all present.

## The dependency closure

`install(RUNTIME_DEPENDENCY_SET)` walks the gtkmm graph and copies it in, with
the system libraries excluded so no second copy of the C runtime or of a macOS
framework gets shipped.

**`file(GET_RUNTIME_DEPENDENCIES)` does not search `PATH` on Windows** — by
design, so a package is not a function of the build shell's environment. That
makes `DIRECTORIES` the entire search path. It is set to the directory holding
`CMAKE_CXX_COMPILER`, which under MSYS2 is the whole UCRT64 prefix: C++ runtime,
libwinpthread and the entire gtkmm stack in one place. The search path is printed
at configure time, because an unresolved DLL names the DLL and never says where
CMake looked.

**The closure has to reach `dlopen`'d modules too.** The pixbuf loaders are in no
target's link graph, so the closure walked straight past their dependencies: an
early `.app` shipped `libpixbufloader_svg.so` and not librsvg, and `dlopen`
failed on `@rpath/librsvg-2.2.dylib`. `file(GET_RUNTIME_DEPENDENCIES)` has a
`MODULES` argument for exactly this, and the loaders go through it. The same hole
would have swallowed libtiff.

That one is invisible on Linux, where a bundled loader finds librsvg from
`/usr/lib` whatever the package contains — so `THINK_PKG_DEPS` is an option
rather than a fact about the platform, and CI turns it on for Linux and requires
librsvg to be *in the package*. A Linux build with it on is not a Linux package
anyone should ship; it was the only way to watch the closure work before a real
machine did. The Windows zip has since been run on one with no GTK, which is
the closure's first end-to-end result.

## The data files the closure cannot see

`cmake/GtkRuntime.cmake`, with the runtime half in `src/gthGtkRuntime.cpp`.
`GET_RUNTIME_DEPENDENCIES` walks the link graph, and four things GTK needs are
not in it:

| | What breaks without it | Bundled |
|---|---|---|
| `gschemas.compiled` | `g_settings_new()` **aborts** on `org.gtk.Settings.FileChooser` | always |
| gdk-pixbuf loaders + cache | no SVG, so no modern Adwaita icons | always |
| `share/mime` index files | SVG is XML, so it is identified by MIME type, not by magic | always |
| Adwaita + hicolor icon themes | plainer icons; GTK's built-in set carries it | when present |

The MIME database was not on the list at all until `-G` was made to decode a real
SVG from the bundled theme and it failed. gdk-pixbuf sniffs a file's leading
bytes against each loader's magic; XML has no distinctive ones, so it falls back
to `g_content_type_guess()`, which reads the freedesktop MIME database via
`XDG_DATA_DIRS`. Without it, a perfectly good SVG comes back "Couldn't recognize
the image file format". Only the top-level index files are shipped — around
400 kB, against 7.5 MB for the per-type XML descriptions nothing here asks for.

Two other things about that table were wrong when it was first written, and both
were caught by running it rather than by reading:

- **The icon theme is not load-bearing.** GTK compiles a fallback icon set into
  its own gresource. With the system themes hidden and nothing bundled,
  `image-missing`, `folder`, `document-open`, `list-add`, `go-up` and `edit-find`
  all still resolve. Bundling Adwaita makes it look right; it does not stop it
  crashing, because it was not going to crash.
- **Windows does need the pixbuf loaders.** MSYS2 builds gdk-pixbuf with
  `-Dbuiltin_loaders=all`, which is why the first draft skipped them there. But
  "all" means all the loaders in gdk-pixbuf's *own* source tree, and SVG is not
  one — it comes from librsvg, as an external module, everywhere.

## Homebrew is keg-based, and that changes three things

**"The GTK prefix" is not one directory.** pkg-config reports the keg path —
`/opt/homebrew/Cellar/gtk4/<version>` — which holds gtk's own files and nothing
else. The schemas are compiled into the *linked* prefix by the formula's
`post_install`, the icon theme belongs to another formula, and librsvg's pixbuf
loader to a third. On Linux and MSYS2 a prefix really is one directory, which is
what let the assumption survive being written down.

So `GtkRuntime.cmake` searches a list of roots — the pkg-config prefixes, the
same paths with `/Cellar/<formula>/<version>` stripped off, `HOMEBREW_PREFIX`,
`CMAKE_PREFIX_PATH` — overridable wholesale with `THINK_GTK_DATA_ROOTS`. When no
prebuilt `gschemas.compiled` turns up in any of them, the XML is compiled here
instead, which is the same work the formula's `post_install` does.

**Homebrew populates its prefix with symlinks back into the Cellar**, and both
`install(FILES)` and `install(DIRECTORY)` preserve symlinks. Those links are
relative to the prefix, so copied into a bundle at a different depth every one of
them dangles: an early macOS package shipped an Adwaita whose icons could be
listed and not opened. Single files are resolved with `REALPATH` before
installing, and directories are copied with `cmake -E copy_directory`, which
dereferences as it goes where `install(DIRECTORY)` does not.

**Resolving the link introduces a second problem.** The link in the prefix and
the file in the keg need not share a basename, so a pixbuf loader installed under
the *resolved* name is a loader the cache — which knows only the *link* name —
cannot find. Each one is installed with an explicit `RENAME`, and an
`install(CODE)` check then verifies that every module the written cache names is
present in the package, so a mismatch is a failed install naming the file rather
than a `dlopen` failure from inside GTK much later.

## The loader cache is the index, not the directory

Copying `gdk_pixbuf_moduledir` wholesale would leave librsvg's loader behind in
its own keg. So every module the cache names is installed from wherever it lives,
and the cache is rewritten to point at where they landed.

The cache format C-escapes its quoted strings, which is why
`gdk-pixbuf-query-loaders` writes `lib\\gdk-pixbuf-2.0\\...` on Windows. Writing
a raw Windows root into it had the parser read
`D:\a\_temp\msys64\tmp\relocated` back as
`D:<BEL>_tempmsys64<TAB>mp<CR>elocated`, and gdk-pixbuf then prefixed the
no-longer-absolute result with its toplevel. The rewritten paths are absolute,
carrying a placeholder that `gthGtkRuntime` substitutes at startup, on every
platform. That is one code path rather than two, and it drops the earlier
dependence on MSYS2's `-Drelocatable=true`: `build_module_path()` passes an
absolute path through unchanged whichever way gdk-pixbuf was built.

## Fonts: nothing to ship

`pango_cairo_font_map_new()` picks CoreText where CoreText and Quartz are both
available, then win32 where cairo has the Win32 surface, and only then
fontconfig. macOS and Windows both take a native backend that reads the fonts
already on the machine. No font files, and no `etc/fonts/fonts.conf` either —
fontconfig is linked but never consulted. Setting `PANGOCAIRO_BACKEND=fc` would
change that; nothing does.

## The application icon: one SVG, three containers

`data/org.thinksynth.thinksynth.svg` is the icon. Linux installs it into
`hicolor/scalable` and the Flatpak takes it from there. Neither of the other two
can use an SVG, so `scripts/make-icons.py` renders it into the containers they
want: `src/thinksynth.ico`, which `src/thinksynth.rc.in` embeds in the
executable, and `src/thinksynth.icns`, which CMake copies into
`thinksynth.app/Contents/Resources`.

The macOS half is two properties in two files, and both are needed — either one
alone leaves the generic icon. `MACOSX_BUNDLE_ICON_FILE` names the file in the
`Info.plist` and sits in `cmake/Packaging.cmake` with the rest of the bundle
metadata; getting the file into `Contents/Resources` is `MACOSX_PACKAGE_LOCATION`,
a *source file* property, and source file properties are scoped to the directory
that defines the target, so it has to be in `src/CMakeLists.txt`. Setting it
beside its sibling would be tidier and would silently do nothing.

**The renderer is gdk-pixbuf's SVG loader, which is librsvg**, reached through
PyGObject — the same library GTK would use to draw the icon, and already present
anywhere this builds. ImageMagick is the obvious alternative and the wrong one:
its built-in SVG renderer ignores the `url(#bg)` gradient and fills the
background flat black. The first version of this script worked around that by
redrawing the icon's geometry by hand in Pillow calls, which meant two files
describing one icon; the workaround was unnecessary, and librsvg had been
reachable the whole time.

**Both renders are committed**, so building for Windows or macOS needs no
rasteriser on the machine doing it. That leaves them able to drift silently —
nothing about editing the SVG makes a stale render stop working — so the
generator writes `src/thinksynth-icon.stamp` with the hash of the SVG it read,
and the `icon-stamp` ctest case compares that against the SVG in the tree. It is
a file hash, so it runs on every platform and needs neither Python nor a
renderer.

The stamp hashes the *input*, not the outputs. Byte-comparing a fresh render
against the committed one would be stricter and unusable: librsvg's antialiasing
differs between versions, so it would fail on whichever machine had a different
one, which is not the mistake worth catching.

**It hashes the SVG with line endings normalised**, which the first version did
not, and it failed on Windows for a file nobody had touched. GitHub's Windows
runners set `core.autocrlf`, so the SVG arrives there byte-for-byte different
from the commit while describing the same drawing. Hashing raw bytes asks "have
these bytes changed?"; the question worth asking is "has the artwork changed?",
and the answer must not depend on which platform is asking. Both the generator
and the check strip `\r\n` first. `.gitattributes` also pins `*.svg` to LF, but
that only helps a fresh checkout — the normalising hash is what covers a working
tree that predates it.

## How any of this is tested without a Mac or a Windows box

`thinksynth -G` reports whether GTK can reach a schema, our icons and a pixbuf
format, and exits nonzero if not. `-DTHINK_BUNDLE_GTK=ON` turns the bundling on
for Linux, where Ubuntu's gdk-pixbuf takes the same `relocatable=false` path
Homebrew's does, so CI installs a bundle, moves it somewhere it was never built
for, and runs `-G` with `XDG_DATA_DIRS` pointed at nothing.

**The negative control runs first**, and it earns its place: two of those three
checks could not fail, GTK having built-in icons and gdk-pixbuf finding the
system cache by a compiled-in path. So CI hides the bundle and requires `-G` to
fail before it believes the run where it passes.

The SVG check went the same way twice over. It began as "is svg in
`gdk_pixbuf_get_formats()`", which reports what the loader *cache declares* —
deleting the loader from a bundle left the check passing. It now decodes an
actual icon from the theme that was shipped. It also counts every `.svg` in the
bundle and requires all of them to be readable, rather than testing whichever one
the directory yields first — that ordering is what let the dangling-symlink bug
pass on Linux and fail on macOS.

One exception, and it is narrow: some Linux distributions build gdk-pixbuf to
delegate to **glycin**, which is configured from `XDG_DATA_DIRS` and lives
outside any bundle, so on such a host this cannot succeed and the failure says
nothing about the package. That case is skipped, matched on gdk-pixbuf naming
glycin in the error. Neither Homebrew nor MSYS2 builds gdk-pixbuf that way, so
the check keeps its teeth exactly where it is needed — verified by removing the
MIME database from a bundle and watching it fail with a non-glycin error.

CI also builds against a fabricated unlinked keg — schema XML and no compiled
blob — so the Homebrew-shaped layout has been through the compile-it-ourselves
path on Linux before a Mac ever reaches it.

## Flatpak

`org.thinksynth.thinksynth.yml`, built by `scripts/build-flatpak-bundle.sh`. It
is not a second copy of the tarball: the tarball links against whatever gtkmm the
machine has, and the Flatpak carries its own on the GNOME runtime, which is the
difference between "works if you already have gtkmm-4" and "works".

Most of that manifest is C++ bindings. `org.gnome.Platform` ships GTK, glib,
pango and cairo but none of gtkmm, glibmm, pangomm, cairomm or libsigc++ —
gnome-build-meta keeps those under `elements/core-deps/`, which builds GNOME OS
rather than the Flatpak SDK. So the manifest builds all six, plus RtAudio and
RtMidi, which are absent from the runtime and from Flathub's shared-modules, and
whose CMake FetchContent fallback cannot run in a build sandbox with no network.

**Version pinning there is done by reading each tarball's `meson.build`** rather
than by taking the newest release: gtkmm 4.22.0 declares `gtk_req '>= 4.22.0'`
and this runtime has GTK 4.20, so 4.22 cannot build against it at all. 4.20.0
asks for `>= 4.19.4` and is the pairing that works.

## What is deliberately not done

No `.pkg`, no MSI, no NSIS, no code signing or notarisation. An unpacked
directory that runs is worth more than an installer nobody has tested, and macOS
will refuse an unsigned `.app` downloaded from the internet regardless — that
wants a developer certificate, which is a decision rather than a task.

## What is not yet verified

**That the macOS `.app` works on a machine that has never had GTK installed.**

Which is every ordinary Mac, and is the whole point of the bundling above:
nobody is going to `brew install gtk4` to run a synth, so `THINK_BUNDLE_GTK`
and `THINK_PKG_DEPS` both default ON for macOS and Windows and the package is
meant to carry its own GTK entire.

**Windows is done.** The zip has been unpacked and run on a machine that has
never had GTK on it, which is the real test and the one CI cannot perform:
building at all means MSYS2 put GTK on the runner, so a bundle that quietly
resolved against the runner's copy would look exactly like one that carried its
own. That result covers the closure, the schemas, the pixbuf loaders and the
icon theme in one go, on the platform where `GET_RUNTIME_DEPENDENCIES` had the
most to get wrong.

macOS has not had the same treatment. It runs — built from source on a Mac,
against the GTK it needed to build in the first place — which says nothing
about the `.app`. Homebrew's keg symlinks and the two-prefix problem are macOS
only, so Windows going green does not carry over.

None of this applies to Linux, where both options default OFF and the package
is expected to use the distribution's GTK. A Linux tarball requires gtkmm
installed and always did; `THINK_BUNDLE_GTK=ON` there exists to exercise the
mechanism, not to produce a package anyone should ship.

CI runs `cpack` on all three platforms and uploads the result as an artefact.
The Linux and Windows packages have been downloaded and run; the macOS `.app`
has not.

**That either platform icon looks right where it is drawn.** Both containers
have been checked structurally — every frame present, correct sizes, the `.icns`
chunk table and declared length parsed back — and the rendering has been looked
at as an image. Neither has been seen in Explorer or in the Finder. The `.icns`
carries no 16x16 @1x chunk, since Pillow's writer emits none; macOS reduces the
32px `ic11` for that slot, which should be indistinguishable, but that is a
prediction and not an observation.
