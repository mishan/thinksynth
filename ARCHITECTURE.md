# Architecture

What the pieces are and how they fit. For the `.dsp` and `.patch` file formats
see [DSP_FORMAT.md](DSP_FORMAT.md); for the output stage and gain staging see
[AUDIO.md](AUDIO.md).

## The layers

~22k lines of C++ in four layers:

| Layer | Path | Role |
|---|---|---|
| Core engine | `libthink/` | Synth graph, arg system, MIDI, DSP-language parser. Builds `libthink.so.6.0`. |
| Plugins | `plugins/` | One `.cpp` per plugin → one module each, `dlopen`ed at parse time. Categories: `osc filt env math mixer delay impulse logic misc analysis dist input fft test visual`. |
| App / IO | `src/` | `main.cpp`, RtAudio + RtMidi, patch file and prefs IO, the node-editor model. |
| GUI | `src/gui/` | gtkmm-4: main window, keyboard, arg sliders, patch selector, MIDI map, node canvas. |

Data lives in `dsp/*.dsp` (92 files, including the `old/` and `noargs/` legacy
directories) and `patches/**/*.patch` (101 presets).

`plugins/visual/` is a different kind of plugin — see
[VISUALIZERS.md](VISUALIZERS.md). It links cairo and not libthink, and has no
node, arg or tree.

## Key classes

- **`thSynth`** — top level. Owns `midiChannels_[]` (`thMidiChan*`), a
  `thPluginManager`, and `treelist_` (a `map<string, thSynthTree*>` of parsed
  DSP files, shared across channels). `process()` is the audio-thread entry
  point.
- **`thSynthTree`** — one parsed `.dsp`: `nodes_` (name → `thNode*`),
  `chanargs_` (the `@foo` user-facing params), an `ionode_`, and a flat
  `nodeindex_` array for fast arg-pointer resolution. Copied per note.
- **`thNode`** — one graph node: a `thPlugin*` and a `map<string,thArg*>` plus
  a parallel `argindex_` array. Holds `children_`/`parents_` lists.
- **`thArg`** — a value. Three flavours: `ARG_VALUE` (constant), `ARG_NODE`
  (pointer to another node's arg), `ARG_CHAN` (reference to a `@chanarg`).
  Emits a sigc++ signal on change — this is how GUI sliders talk to the engine.
- **`thPlugin`** — the `dlopen` wrapper. Plugins export `module_init`
  (registers named args, returns integer indices), `module_callback` (the
  per-window DSP) and `module_cleanup`.

Plugins keep their state *in args* — delay lines, filter history, oscillator
phase. That is not incidental; see [AUDIO.md](AUDIO.md) on why `thArg::allocate()`
must value-initialise.

## Arg direction

`regArg(name, dir)` carries an `ArgDir`, and `argIsPort()` is what keeps the
editor from offering a delay ring for wiring. `ARG_IN` is the default, so a
plugin built against the older header still loads — which is why this did not
need an interface-version bump and `MODULE_IFACE_VER` is still 4.

Three directions matter:

- **in** — read by the plugin, wireable, drawn as an input port.
- **out** — written by the plugin, wireable, drawn as an output port.
- **state** (`ARG_STATE`, conventionally spelled `INOUT_` in a plugin's enum) —
  internal state allocated and read across windows. 69 of these exist and no
  shipped `.dsp` references one. They must never appear on the canvas: nobody
  wants to wire a delay line's internal buffer.

## Arg type

`regArg` says which way an arg faces. Two more things a plugin can say about
one, through `setArgStep` and `setArgValues`:

- **step** — 0 for an ordinary continuous parameter, 1 for one that means a
  whole number.
- **values** — the names of those whole numbers, where they have any. A list
  implies a step of 1, and a range running from its first named entry to its
  last.

Neither is read anywhere in the audio path. They exist because `osc::simple`
reads `waveform` as `switch ((int)buf[i])`, so 3.4 *is* 3 — and a control that
can produce 3.4 is a control most of whose travel does nothing. The eight
shipped patches that drive a waveform all declare `.max = 5.1` or `5.5` rather
than 5, which is an author working around exactly that: padding the top of a
continuous slider so it can still truncate to the last waveform.

Two plugins have a selector to declare, both spelled `waveform`. `osc::simple`
implements six cases; `osc::window` implements three and leaves 1, 4 and 5
commented out with no `default:`, so those values leave the output buffer
unwritten. Its list is six long and names three — an empty name is how the
format says "an index exists here and does nothing".

Six rather than four, because **the length of the list is a statement about the
arg's range**: stopping after the last implemented entry would quietly redefine
what a 5 in a `.dsp` means. What gets offered is decided by the named entries
instead, so the list says six and the control still shows three. A step is
measured from zero, not from the bottom of the range, because the thing being
stepped to is what `(int)x` can distinguish.

### Getting it to the control

A control is a `@chanarg`, and a chanarg is not a plugin arg — the two are
joined only by the wire between them, `in1 = @blim`. So
`thSynthTree::typeChanArgs` walks the tree after `buildArgMap` and carries the
type along that wire. It rides on the same correspondence every plugin already
depends on: a node arg's index equals the plugin's registration index for it,
which is what makes `mod->getArg(node, args[IN_FREQ])` find anything.

**A control is typed only by unanimous agreement.** 197 of the 206 controls in
the corpus drive exactly one parameter, but nine drive several, and a control
feeding both a waveform selector and a mixer gain cannot become a list of six
names without losing the ability to say 0.35 to the mixer. A consumer that says
nothing disagrees with one that does — "no opinion" is the opinion that this is
an ordinary number, and it is held by every arg of every plugin that has not
been taught otherwise.

A `.dsp` can also say it directly, with `@x.step` and `@x.values`, and the file
wins. That needs `thArg::typedByFile` to be a separate flag rather than being
inferred: a step of 0 is the default, so without it an author saying "leave
this one continuous" and an author saying nothing would be the same state.

The rest of the proposal — a description and a default per arg — is still not
done.

**This bumped `MODULE_IFACE_VER` to 5.** Direction did not need one: `regArg`
gained a defaulted parameter and nothing about `thPlugin`'s shape moved, so a
plugin built against the older header still loaded and reported every arg as an
input. This time the three parallel arrays behind `regArg` became one vector,
which moves every member after them — and `setDesc` and `setState` are inlined
*into the plugin* from the header and reach their members by offset. As it
happens both sit before what moved, so a stale plugin would probably survive.
"Probably" is the problem: that is the shape of the stale-`libthink` bug below,
where a mismatched library made every header-inlined accessor read at the wrong
offset and return garbage silently. The version check exists to make that loud,
and stale plugin `.so` files in a source tree are a thing that actually happens
here — `NodePalette`'s own tooltip tells people to go and delete them.

## Threading

There are exactly two threads.

**The audio thread** is RtAudio's callback. It is the sole mutator of
everything reachable from `midiChannels_`.

**The GUI thread** is everything else: the on-screen keyboard, patch
load/unload, preference restore, and MIDI (see below).

They communicate through two lock-free SPSC ring buffers (`thRing.h` — a
command queue, *not* a sample FIFO):

- **Forward queue**, GUI → audio. The GUI does all the allocation — building a
  `thMidiNote` copies an entire synth tree, which must not happen on the RT
  thread — and enqueues a command holding the finished object: `NOTE_ON`,
  `NOTE_OFF`, `ALL_NOTES_OFF`, `SET_CHANNEL`, `SET_CHAN_ARG`. The audio thread
  drains the queue at the top of `process()` and applies each one, which is just
  pointer and container manipulation.
- **Retire queue**, audio → GUI. Whatever the audio thread displaces — the old
  channel, the stolen note, the replaced arg — it hands back, and the GUI frees
  it. Because the audio thread only releases a pointer once it is genuinely
  unreachable, this needs no epoch counting and no grace period.

So `removeChan`, `loadTree` and `setChanArg` are all "build the replacement,
enqueue the swap, free what comes back". No locks on either side.

### What this deliberately does not solve

Slider moves. `ArgTable` calls `setValue()` from the GUI thread while the audio
thread reads the same arg — that is the whole point of a parameter control, and
routing every drag through the queue would be silly. For a single float where
`len_` is already 1, `setValue` does not reallocate, so the worst case is a torn
read of one float. That is what most synths live with; the honest fix is an
atomic or a smoothed parameter rather than a queue. The `setValue(float*, int)`
overload *can* reallocate and does go through the queue.

### It is not hard-RT-safe yet

The per-window path is, now. `thMidiChan::process` used to spell `out0`, `play`
and `trigger` out as `std::string`s and look each one up in a `std::map` — per
channel, per note, per window — and `thSynthTree::getArg` *creates* an arg it
cannot find, so a `.dsp` declaring more channels than it wires up allocated a
`thArg` and inserted it on the first window of every note. Those args are
resolved to indices once, when the channel is built, and any the file did not
write is created there; reaching one in a voice's own tree is now two array
subscripts, because both halves of an arg's address survive the per-note tree
copy. That is the property [thProbe already rides
on](VISUALIZERS.md#the-tap) and the one every plugin's `mod->getArg(node,
args[IN_FREQ])` already uses. The two VLAs sized by window length went the same
way, into buffers allocated with the channel.

`scripts/dsplevel` and a bitwise A/B over the corpus both say the sound did not
move: 81 DSPs, three voices, eight windows, byte for byte.

What is left is real but narrower, and none of it is per window:

- **Plugins allocate on a note's first window.** A note's args are copied from
  the channel's prototype holding a single placeholder value, so the first
  window resizes each of them from 1 to `windowlen` — see [AUDIO.md](AUDIO.md)
  on why that is also where a twenty-year-old noise bug lived. One burst per
  note. Fixing it means sizing a note's args when it is built, on the GUI
  thread, rather than when it is first processed.
- **Note events still look up by name.** `insertNote` and `releaseNote` reach
  `trigger` through a string. Per key press.
- **`thSynthTree::buildSynthTree` walks recursively with a `recalc` flag**
  rather than in topological order.

What the queue design buys is *race freedom* — no torn containers, no
use-after-free — and none of the above is a race.

## Audio and MIDI

Both go through RtAudio and RtMidi, one API each across
CoreAudio/WASAPI/ASIO/ALSA/JACK and CoreMIDI/WinMM/ALSA-seq/JACK-MIDI. They are
callback-driven, which is the model the old JACK path already used.

### The ring between `process()` and the callback

`thSynth::process()` produces exactly `getWindowlen()` frames per call and
cannot be asked for fewer. The device asks for whatever it asks for.

```
  device callback (RT)          gthSynthSource                thSynth
  ------------------            --------------                -------
  wants N frames  ---------->   pop N from ring
                                 ring low?  ---------------->  process()
                                                               push windowlen
                                <-- N frames of float
  clamp, interleave, write
```

`process()` is called from the callback when the ring runs low. That keeps the
"audio thread owns the graph" invariant from the queue rework, at the cost of a
callback that crosses a window boundary doing a whole window's work — so period
jitter is one window wide. The alternative, a dedicated producer thread, would
reintroduce a second thread touching the graph and therefore a second consumer
for a queue that is SPSC.

The ring is four windows deep and primed before `start()` so the first callback
never underruns. `getWindowlen()` is now an internal detail of the synth rather
than a constraint on the device, which is how it should always have been.

This is also a bug fix rather than only a port requirement. The old code did

```c
int copy = ((int)nframes < l) ? (int)nframes : l;
```

and silently discarded the rest of the window. If the device handed it 512
frames, samples 512–1023 were dropped and a fresh window generated — the synth
running at double speed with every other half-window thrown away. Only
`nframes == windowlen` was ever correct, which happened to be true of JACK's
default period on Linux and of nothing else.

### MIDI crosses a thread boundary

RtMidi delivers on its own thread, and Windows has no file-descriptor story to
hand to Glib in any case.

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

`Glib::Dispatcher` is specifically the "wake the GTK main loop from another
thread" mechanism and is implemented on Win32 as well as on Unix. The MIDI ring
reuses `thRing.h` as is.

Going straight from RtMidi's thread into the command queue would skip a hop, but
it loses the `m_sigNoteOn`/`m_sigNoteOff` signals that light up the on-screen
keyboard, and it makes the command queue multi-producer. Not worth it.

Nothing downstream of `thSynth::handleMidiController()` touches a platform API —
`thMidiController`, `thMidiControllerConnection` and `src/gui/MidiMap.cpp` are
all portable.

**One ordering worth not breaking.** `gthMidiQueue::drain` clears its notify
flag *before* the pop loop rather than after. Clearing after leaves a roughly
two-instruction window in which a push sees the flag still set, skips its
`emit()`, and strands a message until the next one arrives — live, that is a
note that hangs. `dspmidi`'s third phase holds this down: rather than racing
for a two-instruction window, it uses a drain hook to stand in it and pushes
from another thread from there. Move the store down past that hook and the
check fails every run.

## Plugin linkage

A plugin exports `apiversion`, `module_init`, `module_callback` and
`module_cleanup`, and imports everything else from libthink.

`generate_export_header(think)` produces a `THINK_API` macro —
`__declspec(dllexport|dllimport)` on Windows, `visibility("default")` elsewhere
— applied to the classes a plugin touches: `thPlugin`, `thArg`, `thNode`,
`thSynthTree`, `thSynth`, `thMidiChan`, `thUtil`. libthink is compiled
`-fvisibility=hidden` on Unix, deliberately: without it, a forgotten `THINK_API`
keeps working on Linux and surfaces as a Windows link error six months later.
Every plugin links against libthink — harmless on Linux and macOS, mandatory on
Windows.

Loading is `dlopen`/`LoadLibraryW` behind one seam in `thPlugin.cpp`. CMake's
`MODULE` library type builds the right kind of object on each platform, and the
suffix is set explicitly from `THINK_PLUGIN_SUFFIX` — the build and the runtime
have to agree on it, since `PLUGIN_SUFFIX` is what `thPluginManager` and
`NodeCatalog` append when they go looking.

## Paths

`thPluginManager::resolveRoot()` and `thUtil::findDataFile()` share one search
order:

```
$THINK_DSP_PATH                    (environment override)
the name as given
<subdir>/<name>                    relative to the cwd
<exe>/../share/thinksynth/dsp      (Unix install)
<exe>/../Resources/dsp             (macOS .app)
<exe>/dsp, ./dsp                   (build tree, Windows install)
DSP_PATH                           (compiled-in default)
```

This is load-bearing rather than cosmetic. Before it existed, `.patch` files
resolved their DSP against a years-old `/usr/local/share/thinksynth/dsp` left
over from some previous `make install`, and the corpus sweep appeared to pass on
the development machine while failing 63 of 99 on a clean runner. Configuring
with `-DCMAKE_INSTALL_PREFIX=/nonexistent` is the way to check this honestly,
because it removes any possibility of a stale install answering.

## Preferences and the first run

`gthPrefs` uses `Glib::get_user_config_dir()`, which gets `~/.config`,
`~/Library/Application Support` and `%LOCALAPPDATA%` right, and writes
`<config>/thinksynth/thinkrc`. A `~/.thinkrc` left by an older version is read
as a fallback when no current file exists, and never written back to.

When neither exists, `gthPrefs::LoadDefaults()` puts four patches on four
channels and `Save()` writes the file. So a fresh install starts with a
keyboard that makes a sound, and what it did is an editable file rather than
hidden state.

The defaults are a table in `gthPrefs.cpp`, not a shipped config file. A file
was what this used to be, `configure_file`d with absolute paths -- correct only
on an installation matching the prefix the build was configured with, and wrong
on every relocatable one. The table names patches relatively and
`gthPatchManager::resolvePatch` finds them through the same search everything
else uses, so the same four lines are right in a build tree, an installed
prefix, a `.app` and a Flatpak.

That resolution is also why the generated file stays portable. A patch is
resolved for *opening* and recorded by the name as given, exactly as
`resolveDsp` already did for DSPs -- so a saved `thinkrc` still says
`leads/SuperRes.patch` and still works after the install moves. `configcheck`
holds that down, because it is invisible until the day it matters.

## If you have thinksynth installed in /usr/local

The soname is at 6. This matters more than it sounds: before the last bump, a
binary built against new headers still resolved the older `libthink.so` from
`/usr/local/lib` at run time, linked without complaint, and then read every
header-inlined accessor at the wrong offset — `thSynth::getChannel` returning
garbage, arg tables coming up empty, the keyboard's channel spinner showing
values like `-733809408`. Silent corruption, no diagnostic.

With the soname bumped the failure is loud instead: `libthink.so.N => not
found`. Run from the build tree, or install first. A stale copy in
`/usr/local/lib` can then sit there harmlessly.
