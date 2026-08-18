# Porting to macOS and Windows

All three platforms build, all three are real CI gates, and all three have
been run and heard on real hardware. The port is done.

This document is the decisions, the build system and the traps. The engine-side
consequences of the port — the audio ring, the MIDI thread hop, plugin linkage
and path resolution — are in [ARCHITECTURE.md](ARCHITECTURE.md), because they
are how the program works now rather than how it was changed. Packaging is in
[PACKAGING.md](PACKAGING.md).

| Platform | State |
|---|---|
| Linux | Builds, runs, audio heard. |
| Windows | Builds, runs, GUI seen, audio heard. |
| macOS | Builds, runs, audio heard. |

What is still unheard is not a platform: no MIDI has been played through the
RtMidi path anywhere, and nothing has listened to the audio path under load.

## 1. Why it was shaped this way

Three things drove the design, and only one of them was the obvious one.

**The plugin symbol model did not work on Windows at all.** Plugins called
`thPlugin::regArg`, `thSynthTree::getArg`, `thArg::allocate` and a dozen other
libthink entry points, and every one was resolved at `dlopen` time against the
host process. That works on Linux because `thinksynth` links libthink with
default visibility, and it worked on macOS *only* via `-flat_namespace -undefined
suppress` — two flags modern ld64 warns about and Apple has been trying to retire
for years. On Windows there is no equivalent: a DLL resolves every symbol at link
time. Fixing it properly meant an export macro across libthink's public headers,
and doing it uniformly on all three platforms was the point — a rule that only
bites on Windows is a rule you keep breaking.

**The audio callback silently threw samples away** whenever the device period
differed from the synth window. Latent on Linux because JACK's default period
happens to be 1024; not latent on any backend that negotiates its own buffer
size, which is every backend on macOS and Windows. So the ring buffer between
`thSynth::process()` and the device callback was not a nicety of the port, it was
the port — and it fixed a bug that existed on Linux. Details in
[ARCHITECTURE.md](ARCHITECTURE.md#the-ring-between-process-and-the-callback).

**Everything else was smaller than the file count suggested.** There is no X11,
no keyboard grab, nothing display-server specific in the GUI tree.
`thEndian.h` already did endianness properly. The command-queue rework had
already made GUI→audio communication lock-free SPSC, which is exactly the shape a
callback-driven backend wants. The engine itself — `libthink/th*.cpp` minus the
plugin loader — is portable C++ with no platform calls in it.

## 2. Decisions

| Question | Answer | Why |
|---|---|---|
| Audio + MIDI | RtAudio + RtMidi | One API each across CoreAudio/WASAPI/ASIO/ALSA/JACK and CoreMIDI/WinMM/ALSA-seq/JACK-MIDI. Callback-driven, which is the model the JACK path already used. MIT, vendorable, ~15k lines total. |
| Build | CMake, everywhere | The hand-rolled `build.mk` + `.dt` dependency scheme could not be made to work on Windows. |
| Plugins | Shared libthink with exported symbols | Keeps `dlopen`-at-parse-time and the drop-a-plugin-in workflow on all three platforms. |
| Windows compiler | MinGW-w64 via MSYS2 (UCRT64) | **MSVC is not an option**: there is no gtkmm for it, and the tree uses variable-length arrays in eleven files (`thMidiChan.cpp:373-374`, `plugins/osc/simple.cpp:93`, and nine more), which are a GCC/Clang extension MSVC has never had. "CMake everywhere" does not mean "MSVC". |
| CI | Build all three + headless harnesses | The harnesses need no audio device and no display, so they run unmodified on hosted runners. |

### Device selection

`-d [rtaudio|jack|none]` plus an API name and a device name, both persisted in
`thinkrc`. `RtAudio::getDeviceInfo` gives real enumeration, so the prefs dialogue
gets a device list for free — there was nothing like it before.

ASIO on Windows is worth having for latency, but the SDK licence forbids
redistributing the headers, so it has to be an off-by-default build option that
the user opts into with a local SDK copy. WASAPI shared mode is the shipping
default and its latency will be noticeably worse than JACK on Linux. **Say so in
the release notes** rather than letting people discover it.

## 3. CMake

```
CMakeLists.txt              options, dependency discovery, install, CPack
cmake/
  ThinkPlugin.cmake         think_add_plugin{,_category}(), think_add_visual()
  Layout.cmake              the three install layouts
  GtkRuntime.cmake          GTK data files for bundles
  Packaging.cmake           CPack + the dependency closure
  RunHarness.cmake          drives the harnesses under CTest
libthink/CMakeLists.txt     bison/flex targets, SHARED lib, export header
plugins/CMakeLists.txt      one think_add_plugin_category() line per category
src/CMakeLists.txt          app + gui
scripts/CMakeLists.txt      the harnesses, and which of them are CTest gates
```

Not every harness is a gate, and the split is worth knowing rather than
counting: `add_test` in `scripts/CMakeLists.txt` is the list, and `dspcheck`
appears in it twice, once for `dsp/` and once for `patches/`. `dspgraph`,
`dsplayout`, `dsplive`, `dspnew`, `dspwrite`, `dspab`, `dspstress` and
`canvasbench` are built and run by hand, because they are slow, need a corpus
argument, or are measuring instruments rather than pass/fail checks.

Three rules worth keeping:

- **Plugin lists stay explicit**, one entry per plugin, never `file(GLOB)`. The
  exclusion of `input/`, `fft/` and `test/` is load-bearing — see
  [DSP_FORMAT.md](DSP_FORMAT.md#5-known-bad-files).
- **Generated sources live in the build tree.** `bison_target(... DEFINES_FILE
  ...)` and `flex_target()` replace the whole `build.mk` bison dance, including
  the `.SECONDARY` workaround that stopped make deleting the generated sources
  every build.
- **`RunHarness.cmake` filters the corpus by file *content*, not by name**, so
  the known-bad exclusions cannot go stale.

`docs/thinksynth.1` is `configure_file`d, and its template keeps the old
`@dsp_path@` spelling with the variables aliased in `CMakeLists.txt`, because
reformatting data files to suit a build system is the wrong way round.

There is no longer an `etc/thinkrc` beside it. It was `configure_file`d the
same way, which baked the configuring machine's absolute dsp and patch
directories into a file that a relocatable package then shipped to someone who
had never had those directories. The defaults a first run gets are built into
the binary instead, named relatively and resolved at runtime -- see
[ARCHITECTURE.md](ARCHITECTURE.md#preferences-and-the-first-run).

### RtAudio and RtMidi cannot come from the distribution

The first attempt made them `REQUIRED` pkg-config dependencies and all three
platforms failed at once. The reason is worse than a naming mismatch: Ubuntu
24.04 does not carry `librtaudio-dev` at all, and 22.04 carries RtAudio **5.2** —
and this code is written against 6, which reports errors by return code rather
than by throwing and identifies devices by opaque id rather than by index. A
system package would be missing on some runners and the wrong API on others.

So the version floor is explicit (`rtaudio >= 6.0.0`) and a source build is the
normal case. CMake fetches and statically links known-good 6.x on every platform;
`-DTHINK_FETCH_RT=ON` forces that path on a machine that has the system packages,
so the fallback is exercised rather than assumed.

One wrinkle worth recording: both projects create a custom target called
`uninstall`, which collide in a single build. Both expose the name as a cache
variable for exactly this reason.

### Autotools is gone

`configure.ac`, `acinclude.m4`, `autogen.sh`, `build.mk.in`, the eleven
`Makefile.in`s, the per-category plugin makefiles,
`config.guess`/`config.sub`/`install-sh`, the standalone `scripts/Makefile` and
the autotools CI workflow: 32 files. The original plan was to retire it once
three-platform CI was green; it came out sooner because keeping both honest was
already costing more than it returned.

There is no `debian/` directory. If Debian packaging comes back it starts from
scratch — the old one was two decades stale and its `control` file still named a
soversion-4 library.

## 4. CI

`.github/workflows/ci.yml`. Three build jobs plus a sanitizer job. They are
independent top-level jobs rather than a matrix, so one platform failing does
not hide the other two.

| Job | Runner | Toolchain | Deps |
|---|---|---|---|
| `linux` | `ubuntu-latest` | GCC | apt: `build-essential cmake ninja-build bison flex pkg-config libgtkmm-4.0-dev libsigc++-3.0-dev libasound2-dev libjack-jackd2-dev adwaita-icon-theme librsvg2-common xvfb` |
| `macos` | `macos-14` (arm64) | Clang | brew: `cmake ninja pkg-config bison flex gtkmm4 adwaita-icon-theme librsvg` — **bison and flex on `PATH` first** |
| `windows` | `windows-latest` | MinGW-w64 UCRT64 | `msys2/setup-msys2@v2` — msys: `bison flex`; pacboy: `toolchain cmake ninja pkgconf gtkmm-4.0 adwaita-icon-theme librsvg` |
| `asan` | `ubuntu-latest` | GCC + ASan/UBSan | as `linux` |

Three things about that table are load-bearing and were each learned the hard
way:

- **No JACK on macOS.** RtAudio and RtMidi are built CoreAudio/CoreMIDI-only
  there, and having Homebrew JACK present is what produced `ld: library jack not
  found` — RtAudio detected it and exported a bare `-ljack` with no `-L`.
- **`adwaita-icon-theme` and `librsvg` are named explicitly** rather than left
  to arrive behind gtkmm. Without librsvg the bundled Adwaita is a directory of
  SVGs nothing can read, which `-G` reports as "svg absent".
- **The Windows package is `gtkmm-4.0`, not `gtkmm4`.** The latter is a virtual
  package, so pacman offers the providers and waits for a choice — which in CI
  is `Enter a selection (default=all): error: target not found`.

The three build jobs run `thinksynth -h` (exits 0; links, loads, prints) and
then `ctest`; the sanitizer job runs `ctest` only. On Linux both run under
`xvfb-run`, because one gate — `editorcheck` — builds real widgets. macOS and
Windows run `ctest` directly.

All four jobs are real gates. What CI cannot cover is the backends themselves:
hosted runners have no audio device, so RtAudio's device-open path is only ever
compiled and linked here.

The sanitizer job applies ASan/UBSan to libthink and the plugins, not just the
harnesses. Nothing currently asserts that the instrumentation actually landed,
which is worth adding — `nm -D build/libthink/libthink.so | grep __asan` is the
check, and a sanitizer job that silently instruments nothing looks exactly like
a clean one.

A `dspstress` TSan job is deliberately left out of the default matrix: TSan
cannot be combined with ASan so it needs its own build tree, and the run is slow.
Worth adding as a nightly `schedule:` trigger once the rest is stable.

**Every job was running twice** at one point, because `on: push` fires for every
branch and `on: pull_request` fires again for the same commits. Pushes are now
limited to `master` and a `concurrency` group cancels superseded runs.

## 5. Traps worth knowing before you hit them

- **Apple ships bison 2.3 and flex 2.5.35.** `/usr/bin/bison` is a GPLv2-era
  fossil that does not understand `--defines=`. Homebrew's `bison` and `flex` are
  keg-only, so `$(brew --prefix bison)/bin` has to go on `PATH` explicitly. This
  catches everyone once.
- **No MSVC, and it is gtkmm that decides that**, not preference. The VLAs
  (eleven sites) and `-fvisibility` idioms follow from the same choice.
- **Hosted CI runners have no audio device.** The harnesses are headless by
  construction, which is fine — but it means CI cannot test the backends
  themselves. RtAudio's device-open path is exercised only by hand. Budget for
  that.
- **gtkmm on macOS runs on the quartz backend and will not look native.** Menus
  land in the window rather than the menu bar and HiDPI is uneven. The app will
  work; it will not look like a Mac application. That is a separate project, and
  note that the node editor is by now a substantial investment in gtkmm
  specifically.
- **A GUI program on Windows has to ask for its console.** The subsystem is a
  link-time flag: console gives every launch a terminal window, GUI gives it
  none and takes stdout and stderr with it. thinksynth links GUI and calls
  `AttachConsole(ATTACH_PARENT_PROCESS)` at the top of `main`, so a terminal
  that launched it gets the output and Explorer does not get a terminal. See
  `src/gthConsole.cpp`. The trap underneath: a redirected stream already has a
  valid handle, and reopening it on `CONOUT$` throws the redirection away.
- **`M_PI` is not in C++, and only Windows makes you notice.** glibc hands it
  out from `<math.h>` because g++ defines `_GNU_SOURCE`, so a file using it
  builds on Linux; MinGW's UCRT hides it behind `_USE_MATH_DEFINES`, so the
  same file fails there and nowhere else. `CMAKE_CXX_EXTENSIONS OFF` is what
  exposes this and is worth keeping for its own reasons. The tree got it wrong
  four times before the fix went anywhere durable: three files carried an
  identical `#ifndef M_PI` shim, a fourth forgot, and a fifth compiled only
  because gtkmm smuggled a definition in. Now the top-level `CMakeLists.txt`
  defines `_USE_MATH_DEFINES` for every target — which fixes the files nobody
  has written yet — and `libthink/thMath.h` is the belt for plugins built out
  of tree, where none of this project's compile definitions apply. The general
  shape: **a platform-conditional macro that one platform hands out for free
  is a trap with a feedback loop measured in CI runs.** Fix it in the build,
  not in the file that noticed.
- **POSIX functions Linux hands out for free.** `setenv` is the one that bit;
  MinGW's UCRT has no such function and the build fails there and nowhere
  else. It is the same shape as the `M_PI` trap above with a different noun,
  and it has the same two answers: use the spelling that is already portable
  where one is to hand (`Glib::setenv`, since glibmm is linked into everything
  with a window in it), and `#ifdef _WIN32` around `_putenv_s` only where it
  is not — `scripts/pathcheck.cpp` is that case, because it deliberately links
  nothing but libthink. The general rule: **if it is in POSIX and not in the C
  or C++ standard, assume the Windows job will tell you about it.**
- **glibmm's deprecated API is compiled out on MSYS2.** `Glib::thread_init()`
  was an undefined reference on Windows for that reason — at link time, not
  compile time. Anything else the tree calls that glibmm has since deprecated
  will fail the same way.
- **Two known-bad patches and eleven known-bad DSPs**, which is why the CI gates
  run over a filtered corpus. See
  [DSP_FORMAT.md](DSP_FORMAT.md#5-known-bad-files).

## 6. What is left

Nothing platform-shaped. Three things the port never covered:

1. **Play MIDI through it**, on any platform. The headless checks pass and no
   device has ever been plugged in. See
   [ARCHITECTURE.md](ARCHITECTURE.md#audio-and-midi).
2. **Listen to the audio path under load** — latency, underruns, and whether
   RtAudio's JACK path holds up against the hand-written one it replaced.
3. **Run the macOS `.app` on a Mac that has never had GTK.** That is what
   bundling GTK into it is for, and a CI runner cannot check it — it had to
   have GTK to build. Windows has passed this; macOS has not. See
   [PACKAGING.md](PACKAGING.md#what-is-not-yet-verified).
