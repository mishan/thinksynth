# thinksynth — macOS and Windows

Survey date: 2026-08-08. Written against `node-editor-layout` @ `6de3270`.

Supersedes the old four-line `PORTING` status table.

| OS | Toolchain | Audio | MIDI | Status |
|---|---|---|---|---|
| GNU/Linux | GCC, CMake | ALSA, JACK | ALSA seq | works |
| macOS | Clang + Homebrew | — | — | configure.ac has a Darwin branch; nothing has been built since ~2006 |
| Windows | — | — | — | never attempted |

> **Progress:** steps 1 and 2 of [section 8](#8-sequencing) are done. CMake
> builds the tree on Linux and produces byte-identical `dspcheck` and
> `dsplevel` output to the autotools build, and CI gates Linux plus a Linux
> ASan/UBSan build on every push. Nothing in sections 3–6 has been touched
> yet, deliberately — see [section 11](#11-what-has-landed).

## 1. The shape of the problem

The audio and MIDI layers are the visible obstacle, and they are real work. But
they are not the *first* obstacle, and two other things are bigger than they
look.

**The plugin symbol model does not work on Windows at all.** The 66 plugins
link against `-lm` and nothing else (`configure.ac:180`, `:184`). They call
`thPlugin::regArg`, `thSynthTree::getArg`, `thArg::allocate` and a dozen other
libthink entry points, and every one of those is resolved at `dlopen` time
against the host process. That works on Linux because `thinksynth` links
libthink with default visibility, and it works on macOS *only* because
`configure.ac:153` passes `-flat_namespace -undefined suppress` — two flags
that modern ld64 warns about and that Apple has been trying to retire for
years. On Windows there is no equivalent: a DLL has to resolve every symbol at
link time. This has to be fixed, and fixing it properly means an export macro
across libthink's public headers.

**The audio callback silently throws samples away whenever the device period
differs from the synth window.** `main.cpp:160-186`:

```c
int copy = ((int)nframes < l) ? (int)nframes : l;
...
for (int k = 0; k < copy; k++)
    buf[k] = thClampSample(synthbuffer[k]);
if ((int)nframes > copy)
    memset(buf + copy, 0, ((int)nframes - copy) * sizeof(float));
process_synth();
```

`l` is `Synth->getWindowlen()`, 1024 by default. If JACK hands you 512 frames,
samples 512–1023 of the window are discarded and a fresh window is generated —
the synth runs at double speed and every other half-window is dropped. If JACK
hands you 2048, you get 1024 frames of audio and 1024 frames of silence,
repeating. Only `nframes == windowlen` is correct.

This is latent on Linux because JACK's default period happens to be 1024 and
the ALSA path calls `Write()` with exactly `getWindowlen()` frames. It stops
being latent the moment you move to a backend that negotiates its own buffer
size, which is every backend on macOS and Windows — CoreAudio will routinely
give you 512, WASAPI shared mode gives you something derived from the device
period that is rarely a power of two you chose. So a ring buffer between
`thSynth::process()` and the device callback is not a nicety of the port; it is
the port. It also fixes a bug that exists on Linux today.

**Everything else is smaller than the file count suggests.** The GUI is already
gtkmm-3 (`configure.ac:241`) — REVIVAL.md still says 2.4, which is stale.
`src/gui/Keyboard.cpp` draws with Cairo and handles `GdkEvent`; there is no
X11, no keyboard grab, nothing display-server specific in the whole GUI tree.
`thEndian.h` already does endianness properly. The command-queue rework means
GUI→audio communication is already lock-free SPSC, which is exactly the shape a
callback-driven backend wants. The engine itself — `libthink/th*.cpp` minus the
plugin loader — is portable C++ with no platform calls in it.

## 2. Decisions

| Question | Answer | Why |
|---|---|---|
| Audio + MIDI | RtAudio + RtMidi | One API each across CoreAudio/WASAPI/ASIO/ALSA/JACK and CoreMIDI/WinMM/ALSA-seq/JACK-MIDI. Callback-driven, which is the model the JACK path already uses. MIT, vendorable, ~15k lines total. |
| Build | CMake, everywhere | The hand-rolled `build.mk` + `.dt` dependency scheme cannot be made to work on Windows. `configure.ac` still regenerates under autoconf 2.73 (checked), but only on deprecation warnings for `AC_TYPE_SIGNAL` and six uses of `AC_HELP_STRING` — it is on borrowed time. |
| Plugins | Shared libthink with exported symbols | Keeps `dlopen`-at-parse-time and the drop-a-plugin-in workflow on all three platforms. |
| Windows compiler | MinGW-w64 via MSYS2 (UCRT64) | **MSVC is not an option**: there is no gtkmm-3 for it, and the tree uses variable-length arrays in eleven files (`thMidiChan.cpp:353-354`, `plugins/osc/simple.cpp:93`, and nine more), which are a GCC/Clang extension MSVC has never had. "CMake everywhere" does not mean "MSVC". |
| CI | Build all three + headless harnesses | `dspcheck` and `dsplevel` need no audio device and no display, so they run unmodified on hosted runners. |

## 3. Audio and MIDI

### 3a. `gthAudio` is the wrong shape

```c++
class gthAudio {
    virtual int Read(void *, int len) = 0;
    virtual int Write(float *, int len) = 0;
    virtual const gthAudioFmt *GetFormat(void) = 0;
    virtual void SetFormat(const gthAudioFmt *fmt) = 0;
    virtual bool ProcessEvents(void) = 0;
};
```

It is push-based, and only one of the three implementations actually uses it
that way:

- **`gthALSAAudio`** is the only real `Write()` user. It runs its own thread on
  a private `Glib::MainContext`, polling ALSA's descriptors and sleeping a
  millisecond between iterations (`gthALSAAudio.cpp:369-372`). That is not a
  real-time thread and never was.
- **`gthJackAudio`** does not implement the interface in any meaningful sense.
  `Write()` and `Read()` are no-ops (`gthJackAudio.cpp:264`, `:269`). The
  actual audio path is `playback_callback`, a free function in `main.cpp` that
  reaches for the global `Synth` and calls `jack->GetOutBuf()` directly. JACK
  is wired *around* `gthAudio`, not through it.
- **`gthDummyAudio`** returns zero from everything.

`Read()` returns -1 or 0 in all three; nothing calls it. `ProcessEvents()`
returns false everywhere and its ALSA body is dead code the author marked `XXX`
(`gthALSAAudio.cpp:327-343`). Both can go.

The replacement is callback-shaped, which every backend worth having already
is:

```c++
class gthAudio {
public:
    struct Config { int rate; int channels; int frames; std::string device; };

    virtual ~gthAudio() {}
    virtual bool open(const Config &) = 0;
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual const gthAudioFmt *format() const = 0;

    /* Enumeration the current code has none of, and which the prefs UI wants. */
    virtual std::vector<DeviceInfo> devices() const = 0;
};
```

with **one** renderer shared by every backend, holding the clamp/limit logic
that currently lives inline in `playback_callback`.

### 3b. The ring between `process()` and the callback

`thSynth::process()` produces exactly `getWindowlen()` frames per call and
cannot be asked for fewer. The device asks for whatever it asks for. So:

```
  device callback (RT)          gthSynthSource                thSynth
  ------------------            --------------                -------
  wants N frames  ---------->   pop N from ring
                                 ring low?  ---------------->  process()
                                                               push windowlen
                                <-- N frames of float
  clamp, interleave, write
```

Two ways to place `process()`:

- **Call it from the callback when the ring runs low.** Simplest, keeps the
  current "the audio thread owns the graph" invariant from the queue rework,
  and `process()` is already the thing the JACK callback calls. Downside:
  worst-case callback cost is one whole window even when only 64 frames were
  asked for, so period jitter is a window wide.
- **A dedicated producer thread** filling the ring, with the callback only ever
  popping. Smoother, but reintroduces a second thread touching the graph and
  therefore a second consumer for the command queue, which is a SPSC ring
  (`thRing.h`). That would need genuine rework, not just a thread.

Take the first. Size the ring at four windows so a late callback has slack, and
prime it before `start()` so the first callback never underruns. `thRing.h` is
a single-producer/single-consumer queue for commands, not a sample FIFO — this
wants a small separate float ring, ~60 lines.

Once this exists, `getWindowlen()` becomes an internal detail of the synth
rather than a constraint on the device, which is how it should always have
been.

### 3c. MIDI has to cross a thread boundary now

Today MIDI never leaves the GUI thread. ALSA's sequencer descriptors are handed
to `Glib::signal_io()` (`gthALSAMidi.cpp:127-129`), so `pollMidiEvent` and
therefore `processmidi()` (`main.cpp:193`) run in the GTK main loop. That is
why `addNote` is allowed to copy an entire synth tree — it is not on the audio
thread.

RtMidi delivers on its own thread, and Windows has no file-descriptor story to
hand to Glib in any case. The portable arrangement:

```
RtMidi callback thread          GUI thread (GTK main loop)      audio thread
----------------------          --------------------------      ------------
push 3 bytes into SPSC ring
Glib::Dispatcher::emit()  ---->  drain ring
                                 processmidi() logic
                                   Synth->addNote()      ----->  command queue
                                   m_sigNoteOn()                 drain in process()
                                   (on-screen keyboard)
```

`Glib::Dispatcher` is the right primitive: it is specifically the "wake the GTK
main loop from another thread" mechanism, and it is implemented on Win32 as
well as on Unix. The MIDI ring can reuse `thRing.h` as-is.

Nothing downstream changes. `thMidiController`, `thMidiControllerConnection`
and `src/gui/MidiMap.cpp` all sit behind `thSynth::handleMidiController()` and
never touch a platform API.

The alternative — pushing MIDI straight from RtMidi's thread into `thSynth`'s
command queue — skips a hop but loses the `m_sigNoteOn`/`m_sigNoteOff` signals
that light up the on-screen keyboard, and makes the command queue
multi-producer. Not worth it.

### 3d. What to delete

Once `gthRtAudio` and `gthRtMidi` work on Linux:

- **`gthALSAAudio.{h,cpp}`, `gthALSAMidi.{h,cpp}`** — RtAudio and RtMidi both
  have native ALSA backends that do the same job without the 1 kHz spin loop,
  the detached-thread lifetime problem, or the dead `ProcessEvents` body.
- **`gthJackAudio.{h,cpp}`** — RtAudio exposes JACK as an API, and its JACK
  backend auto-connects to physical playback ports, which is what
  `tryConnect()` (`gthJackAudio.cpp:94-217`) hand-rolls. This one is worth
  keeping around behind a build flag until you have listened to the RtAudio
  JACK path under load, because JACK users are the audience that will notice.
- **`src/old/`** — `thOSSAudio.cpp`, `monotest.cpp`, `thWav.cpp`, not in the
  build, referencing OSS.

That is roughly 1,400 lines out and roughly 500 in.

### 3e. Device selection and prefs

`-d [jack|alsa]` and `-o [device]` become `-d [rtaudio|jack|none]` plus an API
name and a device name, and `.thinkrc` should persist both. `RtAudio::getDeviceInfo`
gives real enumeration, so the prefs dialogue gets a device list for free —
there is nothing like it today.

ASIO on Windows is worth having for latency but the SDK licence forbids
redistributing the headers, so it must be an off-by-default build option that
the user opts into with a local SDK copy. WASAPI shared mode is the shipping
default and its latency will be noticeably worse than JACK on Linux. Say so in
the release notes rather than letting people discover it.

## 4. Plugins: linkage and loading

### 4a. Symbols

Currently a plugin exports three things and imports everything else from thin
air:

```c++
unsigned char apiversion = MODULE_IFACE_VER;   /* thPlugin.h:29 */
extern "C" int  module_init(thPlugin *);
extern "C" int  module_callback(thNode *, thSynthTree *, unsigned, unsigned);
```

The fix is uniform across all three platforms, and doing it uniformly is the
point — if it only bites on Windows you will keep breaking it.

1. **`generate_export_header(think)`** in CMake produces a `THINK_API` macro:
   `__declspec(dllexport|dllimport)` on Windows,
   `__attribute__((visibility("default")))` elsewhere. Apply it to the classes
   a plugin touches: `thPlugin`, `thArg`, `thNode`, `thSynthTree`, `thSynth`,
   `thMidiChan`, `thUtil`.
2. **Compile libthink with `-fvisibility=hidden`** on Unix. Without this the
   Linux build keeps working when someone forgets a `THINK_API` and the mistake
   only surfaces as a Windows link error six months later.
3. **Link every plugin against libthink** (`target_link_libraries(<plugin> PRIVATE think)`).
   Harmless on Linux and macOS, mandatory on Windows.
4. **Drop `-flat_namespace -undefined suppress`** from the macOS flags
   (`configure.ac:153`). Once plugins link against libthink they do not need
   it, and it is deprecated on current ld64.
5. **A `THINK_PLUGIN_EXPORT` macro** in `thPlugin.h` under `#ifdef PLUGIN_BUILD`,
   applied to `apiversion`, `module_init`, `module_callback` and
   `module_cleanup`. One header change covers all 66 plugins.

While in there, a live bug worth fixing in the same sweep. `thPlugin.h:37`
declares `void module_cleanup(struct module *)` — and `struct module` is a type
that does not exist anywhere in the tree; it is being forward-declared into
existence by the parameter list. All 66 plugins define it with that signature.
But `thPlugin.h:64` typedefs `ModuleCleanup` as `void (*)(thPlugin *)`, and
`thPlugin.cpp:208` does

```c++
module_cleanup = (ModuleCleanup)dlsym(handle_, "module_cleanup");
if (module_cleanup != NULL)
    module_cleanup(this);
```

so every plugin's cleanup hook is called through a function pointer of the
wrong type, on every unload. It survives only because all 66 bodies are empty.
Pick one signature.

### 4b. Loading

`thPlugin.cpp:25-33` already has a `HAVE_DLFCN_H` / `USING_DARWIN` split, with
`nsmodule_dl.cpp` implementing `dlopen` over the NSModule API. **Delete
nsmodule_dl.** macOS has had a real `dlopen(3)` since 10.3 in 2003;
`HAVE_DLFCN_H` is true there, so the shim is already dead code, and NSModule
itself has been deprecated for about as long.

Add a Windows arm: `LoadLibraryW` / `GetProcAddress` / `FreeLibrary`, ~40 lines
behind the same seam. Suffix becomes `.dll`; keep `.dylib` on macOS rather than
`.bundle` — CMake's `MODULE` library type does the right thing on each platform
and one fewer special case in `PLUGIN_SUFFIX` is worth having.

### 4c. Directory scanning

Two `opendir`/`readdir` walks: `thPluginManager.cpp:56-104` (`hasPlugins`) and
`NodeCatalog.cpp:61-118` (the node-editor palette). Both become
`std::filesystem::directory_iterator`, which also gets path separators right
for free. This means moving the tree from `-std=c++11` (`configure.ac:108`) to
C++17 — REVIVAL.md §2 already records that it compiles clean under C++17.

### 4d. Finding the executable

`thPluginManager.cpp:113` does `readlink("/proc/self/exe")` and returns `""`
everywhere else, which means the exe-relative plugin search silently does
nothing off Linux. Add `_NSGetExecutablePath` (macOS) and `GetModuleFileNameW`
(Windows). This is what makes a relocatable `.app` bundle and a Windows install
directory work at all, so it is not optional.

## 5. Paths, config and data

| Site | Problem | Fix |
|---|---|---|
| `gthPrefs.cpp:49` | `string(getenv("HOME")) + "/" + PREFS_FILE` — unchecked, and wrong on Windows | `Glib::get_user_config_dir()`. gtkmm is already a dependency and it gets `~/.config`, `~/Library/Application Support` and `%LOCALAPPDATA%` right. |
| `thUtil.cpp:58`, `:74` | `basename`/`dirname` via `strrchr(path, '/')` | `std::filesystem::path::filename()` / `parent_path()` |
| `gthPatchfile.cpp:78` | `dspName[0] == '/'` as the absolute-path test | `fs::path::is_absolute()` — handles `C:\` and UNC |
| `configure.ac:373-375` | `DSP_PATH`, `PATCH_PATH`, `PLUGIN_PATH` baked in at configure time | Runtime resolution order, below |
| `configure.ac:362` | `DEFAULT_THINKRC` = `$sysconfdir/thinkrc` | Same |
| `autogen.sh:19` | hardcoded `/usr/local/share/aclocal` | Deleted along with autotools |

`thPluginManager::resolveRoot()` (`:127-168`) already implements the right idea
for plugins — environment override, then preferred, then cwd-relative, then
exe-relative. Generalise it into one helper and use it for DSPs and patches
too, with the macOS bundle layout (`../Resources/dsp`) added to the candidate
list:

```
$THINK_DSP_PATH
<exe>/../share/thinksynth/dsp      (Unix install)
<exe>/../Resources/dsp             (macOS .app)
<exe>/dsp, ./dsp                   (build tree, Windows install)
DSP_PATH                           (compiled-in default)
```

## 6. Mechanical cleanups the port forces

None of these are hard; all of them have to happen before a second compiler
sees the tree.

- **`char *desc = "..."` in all 66 plugins** (verified: every one). Ill-formed since C++11 —
  a warning under GCC, an error under Clang's newer defaults. Already item 2 of
  REVIVAL.md §5; the port makes it mandatory rather than tidy.
  `static const char desc[] = ...` and `setDesc` already takes `const string &`.
- **`using namespace std;` in `think.h`** (`:31`) — a public header included by
  every plugin. Harmless until `<windows.h>` arrives with its `min`/`max`
  macros. Define `NOMINMAX` and `WIN32_LEAN_AND_MEAN` centrally now; drop the
  `using` from the header when there is appetite for the churn.
- **`M_PI`** is not in `<cmath>` without `_USE_MATH_DEFINES` on some toolchains
  and several oscillators use it (`plugins/osc/bandosc.cpp` among them). Define
  it defensively in `think.h`.
- **`pthread_mutex_t`** (`thSynth.cpp:28`, `:39-40`, `:74-75`). Not vestigial —
  there are twenty-odd `pthread_mutex_lock`/`unlock` pairs still in the file
  (`:303`, `:433`, `:476`, `:503`, `:533`, `:663`, `:760`, `:800`, `:827`,
  `:936` and their partners). What the queue rework removed was the *audio
  thread's* participation; the GUI-side operations still serialise against each
  other through it. MinGW-w64 ships winpthreads so this compiles as-is, but
  `std::mutex` is a smaller surface and the tree is already on C++11.
- **`getopt`** (`main.cpp:317`) — provided by MinGW-w64, so this survives, but
  it is worth knowing it is not a given.
- **`-ffast-math` on everything including the GUI and the link line**
  (`configure.ac:108`). Sets FTZ/DAZ process-wide. Drop it in the CMake move;
  if any DSP depends on it, that DSP has a problem.

## 7. CMake

```
CMakeLists.txt              options, dependency discovery, install, CPack
cmake/
  ThinkPlugin.cmake         think_add_plugin()
libthink/CMakeLists.txt     bison/flex targets, SHARED lib, export header
plugins/CMakeLists.txt      one think_add_plugin() line per plugin
src/CMakeLists.txt          app + gui
scripts/CMakeLists.txt      the harnesses, registered with CTest
```

What it replaces and what it buys:

- `bison_target(... DEFINES_FILE ...)` and `flex_target()` replace the entire
  `build.mk` bison dance — including the `.SECONDARY` workaround that stops
  make deleting the generated sources every build, and the `--defines=` fix
  from the last round.
- `add_library(think SHARED)` with `VERSION`/`SOVERSION` from `lib_major`
  reproduces `libthink.so.6.0` and its soname, and gets the equivalent
  `install_name` on macOS and the import library on Windows.
- `generate_export_header` produces `THINK_API` (§4a).
- `add_library(<plugin> MODULE)` is exactly right: `-bundle` on macOS, a plain
  `.dll` on Windows, `.so` on Linux, without a `PLUGIN_SUFFIX` variable at all.
- `pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0)` works on all
  three (MSYS2 ships pkgconf; Homebrew ships pkg-config).
- The per-file `.dt` dependency hack and `AC_CXX_MT_BROKEN` disappear.
- `install()` + CPack gives `.tar.gz`, a `.dmg` and an NSIS installer later
  without a second packaging system.

Keep the plugin lists **explicit**, one entry per plugin, not `file(GLOB)`. The
current per-category Makefiles deliberately exclude `input/`, `fft/` and
`test/`, and that exclusion is load-bearing — `wav.cpp` describes itself as
`"Wav Input (BROKEN)"` and eleven shipped DSPs already fail `dspcheck` because
they reference those plugins.

Autotools is **gone**, in the commit after the CMake one. The original plan
said to retire it once three-platform CI was green; it came out sooner because
keeping both honest was already costing more than it returned. `configure.ac`,
`acinclude.m4`, `autogen.sh`, `build.mk.in`, the eleven `Makefile.in`s, the
per-category plugin makefiles, `config.guess`/`config.sub`/`install-sh`, the
standalone `scripts/Makefile` and the autotools CI workflow: 32 files.

`etc/thinkrc` and `docs/thinksynth.1` were generated by `configure` and are
now `configure_file`d by CMake; their templates keep the old `@dsp_path@`
spelling, with the variables aliased in `CMakeLists.txt`, because reformatting
data files to suit a build system is the wrong way round. `debian/rules` was
translated to drive CMake -- untested, and the packaging was already two
decades stale (`debian/control` still says `libthink4` against a soversion-6
library).

## 8. Sequencing

Deliberately front-loaded on Linux: every step up to 5 is verifiable on the
machine where you can actually hear whether you broke something, and by the
time macOS and Windows arrive there is a working net under you.

1. ~~**CMake on Linux, byte-comparable to autotools.**~~ Done — section 11.
2. ~~**Linux CI** — build, `dspcheck` over `dsp/` and `patches/`, `dsplevel`,
   plus an ASan/UBSan job.~~ Done — section 11.
3. **Platform-independent cleanups**, all on Linux: §6 in full, `std::filesystem`
   for the two directory walks and `thUtil`, C++17, `THINK_API` + `-lthink` on
   plugins + `-fvisibility=hidden`, delete `nsmodule_dl`, `Glib::get_user_config_dir()`,
   generalised path resolution.
4. **The audio rework**, on Linux: reshape `gthAudio` to callback form, add the
   sample ring (§3b — this fixes the window/period bug that exists today), bring
   `gthRtAudio` up against ALSA and JACK. `scripts/dsplive` and `scripts/dspab`
   are the tools for judging it.
5. **The MIDI rework**, on Linux: RtMidi + `thRing` + `Glib::Dispatcher`, delete
   `gthALSAMidi`.
6. **macOS.** By this point the genuinely new work is `_NSGetExecutablePath`,
   bundle-relative resources, and the Homebrew bison/flex trap (§9). Add the
   macOS CI job.
7. **Windows.** MSYS2/UCRT64. The DLL export work landed in step 3, so what is
   left is `GetModuleFileNameW`, the `.dll` loader arm, and `%LOCALAPPDATA%`.
   Add the Windows CI job.
8. **Packaging.** `.app` bundle with `install_name_tool`/`macdeployqt`-style
   rpath fixup on macOS; a zip or NSIS installer carrying the MinGW DLL closure
   on Windows. gtkmm-3 drags roughly forty DLLs behind it, so this is a real
   step, not an afterthought.

Steps 1–3 are worth doing even if the port stops there: they fix a real audio
bug, remove a deprecated macOS linker hack, and delete a thousand lines.

## 9. Traps worth knowing before you hit them

- **Apple ships bison 2.3 and flex 2.5.35.** `/usr/bin/bison` is a GPLv2-era
  fossil that does not understand `--defines=`, which `build.mk.in:16` and
  `libthink/Makefile.in:50` both depend on. Homebrew's `bison` and `flex` are
  keg-only, so `$(brew --prefix bison)/bin` has to go on `PATH` explicitly. This
  catches everyone once.
- **gtkmm-3 on macOS runs on the quartz backend and looks non-native.** Menus
  land in the window rather than the menu bar, HiDPI is uneven, and `Gtk::Main`
  and `Glib::thread_init` (`main.cpp:290`, `:292`) are deprecated though still
  present. The app will work. It will not look like a Mac application. That is
  a separate project, and note that the node editor — `NodeCanvas`,
  `NodeGraph`, `NodeEdit`, ~120 KB of Cairo drawing — is now a substantial
  investment in gtkmm specifically.
- **No MSVC, and it is gtkmm that decides that**, not preference. The VLAs
  (eleven sites) and `-fvisibility` idioms follow from the same choice.
- **Hosted CI runners have no audio device and no display.** That is fine:
  `dspcheck`, `dsplevel` and `dspstress` are headless by construction. It does
  mean CI cannot test the backends themselves — RtAudio's device-open path is
  exercised only by hand. Budget for that.
- **Two known-bad patches and eleven known-bad DSPs.**
  `patches/pads/Rythmic.patch` and `Rythmic-2.patch` point at
  `/usr/local/share//thinksynth/dsp/mfm03.dsp`, which does not exist —
  leftovers from the "don't use absolute paths" cleanup. And eleven of the 92
  shipped DSPs reference `input/wav`, `input/alsa` or `misc/wlan`, plugins that
  compile but are deliberately not in `PLUGIN_DIRS`. So the CI gates run over
  **81 of 92 DSPs and 99 of 101 patches**, and the workflow filters by what a
  file *references* rather than by name so the exclusion cannot go stale. Fix
  the two patches and the filter shrinks by itself.

## 10. CI

`.github/workflows/ci.yml`. Three build jobs plus a sanitizer job,
`fail-fast: false` so one platform failing does not hide the other two.

| Job | Runner | Toolchain | Deps |
|---|---|---|---|
| `linux` | `ubuntu-latest` | GCC | apt: `cmake ninja-build bison flex libgtkmm-3.0-dev libsigc++-2.0-dev libasound2-dev libjack-jackd2-dev` |
| `macos` | `macos-14` (arm64) | Clang | brew: `cmake ninja bison flex pkg-config gtkmm3 jack` — **bison and flex on `PATH` first** |
| `windows` | `windows-latest` | MinGW-w64 UCRT64 | `msys2/setup-msys2@v2`, pacman: `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,gtkmm3,rtaudio,rtmidi} bison flex` |
| `asan` | `ubuntu-latest` | GCC + ASan/UBSan | as `linux` |

Every job runs the same three gates after building:

```sh
./src/thinksynth -h                                   # exits 0; links, loads, prints
ctest --output-on-failure                             # wraps the harnesses:
  dspcheck -q -p plugins/ $(find dsp -name '*.dsp')       # exit = files failed
  dspcheck -q -p plugins/ $(find patches -name '*.patch') # exit = files failed
  dsplevel  -p plugins/ dsp/ts1.dsp ...                   # exit = peaks over TH_MAX
```

`dspcheck` is the valuable one — it loads every DSP, plays a chord, overruns
polyphony, releases, reloads onto the same channel and tears down, and it also
renders each note twice and compares bitwise to catch uninitialised plugin
state. Under ASan that is most of REVIVAL.md's crash inventory re-checked on
every push, on three platforms.

A `dspstress` TSan job is deliberately left out of the default matrix: TSan
cannot be combined with ASan so it needs its own build tree, and the run is
slow. Worth adding as a nightly `schedule:` trigger once the rest is stable.

`.github/workflows/ci-linux-autotools.yml` is gone along with the build it
guarded — see §7.

## 11. What has landed

### CMake (§8 step 1)

```
CMakeLists.txt              options, probes, config.h, install, the status banner
cmake/config.h.in           replaces autoheader's config.h
cmake/ThinkPlugin.cmake     think_add_plugin()
cmake/RunHarness.cmake      the corpus sweeps, as a `cmake -P' script
libthink/CMakeLists.txt     bison/flex targets, shared + static, SOVERSION
plugins/CMakeLists.txt      62 plugins, explicit lists
src/CMakeLists.txt          app, GUI, and the GTK-free node model as an object lib
scripts/CMakeLists.txt      9 harnesses, 3 CTest gates
```

Verified against the autotools build on the same tree:

| | autotools | CMake |
|---|---|---|
| `libthink` | `libthink.so.6.0` | `libthink.so.6.0` |
| plugins built | 62 | 62 (identical set) |
| harnesses | 9 | 9 |
| `dspcheck` over 81 DSPs | 81/81, exit 0 | **byte-identical output** |
| `dspcheck` over 99 patches | 99/99, exit 0 | **byte-identical output** |
| `dsplevel -v 4` | exit 0 | **byte-identical output** |

`-ffast-math` is kept, deliberately, behind `THINK_FAST_MATH=ON`. §6 wants it
gone, but dropping it changes floating-point results, and doing that in the
same change as a build-system swap would mean any difference had two possible
causes. It is one flag flip when §8 step 3 gets there.

Three things worth knowing:

- **The soname changed, on purpose.** `build.mk` passed
  `-Wl,-soname,libthink.so.6.0`, so the *minor* was part of the soname and
  every minor bump was an ABI break — which contradicts `configure.ac`'s own
  comment about what `lib_minor` means. CMake's `SOVERSION` gives
  `libthink.so.6`. The filename is unchanged, so REVIVAL.md's warning about
  stale `/usr/local/lib` copies still holds and is if anything less likely to
  bite.
- **Generated sources now go to the build tree.** `libthink/thinklang.{cpp,h}`
  and `thinklex.cpp` stop being written into `libthink/`. The `.gitignore`
  entries for them can go when autotools does. The header install had to learn
  to exclude them from its source-dir glob, or a tree with an autotools build
  lying around installs the stale copies.
- **`RunHarness.cmake` computes the file lists at test time, not configure
  time.** Which DSPs are eligible depends on what each file *references* —
  eleven reference `input/wav`, `input/alsa` or `misc/wlan`, which are not in
  the built set. Filtering by name would go stale the moment one is fixed;
  filtering by content means building the missing plugin adds its DSPs to the
  sweep by itself. Doing it in CMake script mode rather than shell is what
  makes the same gates run on Windows later.

What was deliberately **not** done, because it belongs to §8 step 3 and would
have made this build non-comparable: plugins are still not linked against
libthink, there is no `THINK_API` export macro, the tree is still C++11, and
`nsmodule_dl` is still present (though it is not compiled — it was Darwin-only
and is dead code either way).

### CI (§8 step 2)

`ci.yml` now runs four jobs. `linux` and `asan` are real gates. `macos` and
`windows` are `continue-on-error` — they run so the log shows how far the port
gets, without turning the workflow red before §8 steps 6 and 7 have happened.
Remove the flag from each as it comes good.

The ASan job was checked for the failure mode REVIVAL.md records: `nm -D` on
`libthink.so.6.0` and on `plugins/osc/simple.so` both show `__asan` symbols, so
the instrumentation genuinely reaches the library and the plugins rather than
only the harness.
