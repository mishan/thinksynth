# thinksynth — revival survey

Survey date: 2026-08-07. Written against `master` @ `1e4905d`.

> **Status:** Tier 0 is done, along with the output-stage static, the leaks,
> tree ownership and the concurrency rework. See [section 6](#6-whats-been-fixed)
> for what changed and how to re-run the checks.

## 1. What's here

~22k lines of C++, autotools build, four layers:

| Layer | Path | Role |
|---|---|---|
| Core engine | `libthink/` | Synth graph, arg system, MIDI, DSP-language parser. Builds `libthink.so.4.0`. |
| Plugins | `plugins/` | 66 `.cpp` files → one `.so` each, `dlopen`ed at parse time. Categories: `osc filt env math mixer delay impulse logic misc analysis dist input fft test`. |
| App / IO | `src/` | `main.cpp`, ALSA + JACK audio, ALSA MIDI, patch file + prefs IO. |
| GUI | `src/gui/` | gtkmm-2.4: main window, keyboard, arg sliders, patch selector, MIDI map. |

Data: `dsp/*.dsp` (~90 patches, incl. `old/` and `noargs/` legacy dirs), `patches/**/*.patch` (~50 presets).

### Key classes

- **`thSynth`** — top level. Owns `midiChannels_[]` (`thMidiChan*`), a `thPluginManager`, and `treelist_` (a `map<string, thSynthTree*>` of parsed DSP files, shared across channels). `process()` is the audio-thread entry point.
- **`thSynthTree`** — one parsed `.dsp`: `nodes_` (name → `thNode*`), `chanargs_` (the `@foo` user-facing params), an `ionode_`, and a flat `nodeindex_` array for fast arg-pointer resolution. Copied per-note.
- **`thNode`** — one graph node: a `thPlugin*` and a `map<string,thArg*>` plus parallel `argindex_` array. Holds `children_`/`parents_` lists.
- **`thArg`** — a value. Three flavours (`ARG_VALUE` constant, `ARG_NODE` pointer to another node's arg, `ARG_CHAN` reference to a `@chanarg`). Emits a sigc++ signal on change — this is how GUI sliders talk to the engine.
- **`thPlugin`** — `dlopen` wrapper. Plugins export `module_init` (registers named args, returns integer indices), `module_callback` (the per-window DSP), `module_cleanup`.

### The DSP language

Bison/flex (`libthink/thinklang.yy`, `thinklex.ll`). It is already a node-graph description language:

```
name "TS-1";
author "Leif Ames";

@cutoff = 4;              # a channel arg = user-facing knob
@cutoff.widget = 1;       # .min .max .label .widget
@cutoff.min = 0;
@cutoff.max = 16;

node freq misc::midi2freq {     # <name> <category>::<plugin>
    note = ionode->note;        # edge: another node's arg
};
node cutcalc math::mul {
    in0 = freq->out;
    in1 = @cutoff;              # edge: a channel arg
};
node ionode { out0 = mixer->out; channels = 2; play = env->play; };
io ionode;
```

Values support units (`5 ms`, `90%`), arithmetic, and the constant `th_max`. This maps 1:1 onto a visual node editor — nodes, typed ports, edges — with essentially no impedance mismatch.

### The patch format

`.patch` is *not* a graph. It's a reference to a `.dsp` plus flat overrides:

```
dsp ts1.dsp
info author Leif Ames
info title Phat Rip
cutoff 8.809662
res 2.854232
```

So a `.patch` is a preset over a DSP's `@chanargs`. `src/gui/ArgTable.cpp` already renders these as sliders.

## 2. Build & dependency state

**Good news: the code compiles clean under GCC 16 / C++17.** I syntax-checked all 66 plugins, `libthink`, and `src/gui` — zero errors except `plugins/test/test.cpp` (calls `SetState`/`SetDesc`, renamed to `setState`/`setDesc` long ago; it's already excluded from the build). Warnings only, dominated by `char *desc = "..."` in ~66 files.

Dependency risk, in order:

1. **gtkmm-2.4** — EOL since 2019/2020, still packaged on Debian/Ubuntu but on borrowed time. This is the single largest liability. gtkmm-3.0 is the low-friction port; gtkmm-4.0 is a rewrite of the widget layer.
2. **libsigc++-2.0** — still maintained, but sigc++-3.0 is current and gtkmm-4 requires it. Coupled to the gtkmm decision.
3. **gthread-2.0** — deprecated; folded into glib since 2.32. The `PKG_CHECK_MODULES(GTHREAD, ...)` can just go away.
4. **JACK** — fine. `pipewire-jack` is a drop-in on modern distros.
5. **ALSA** — fine.
6. **autotools** — works, but `configure.ac` uses `AC_HELP_STRING` (removed in autoconf 2.70+), `AC_TYPE_SIGNAL` (obsolete), and a hand-rolled `build.mk` with per-file `.dt` dependency hacks. It will fight newer autoconf.
7. **bison/flex** — 3.8.2 / 2.6.4 present and working; the grammar uses the old `%pure_parser`-era idioms but generates fine.

Also: `-ffast-math` is applied to the *entire* program including the GUI and the link line (`configure.ac:84`). That sets FTZ/DAZ process-wide and gives the optimizer license the code doesn't want.

## 3. Crash inventory

A full audit is below, ordered by expected payoff. Items 1–8 are the ones most likely behind the crashes you're seeing.

### Tier 0 — near-certain crashes, cheap fixes

1. **`thSynthTree::nodeindex_` is never initialized.** Neither constructor (`thSynthTree.cpp:27`, `:36`) assigns it; the destructor (`:53`) does `delete[] nodeindex_` on it. Any tree destroyed before `buildNodeIndex()` → heap corruption. One-line fix.
2. **`nodeindex_` is allocated uninitialized and only partially filled** (`thSynthTree.cpp:432`). The copy ctor only copies nodes reachable from the ionode, leaving garbage pointers in the holes. `getArg` dereferences those in the audio thread. Fix: `new thNode*[nodecount_]()` + null-check every deref.
3. **`YYPARSE()` return value is never checked** (`thSynth.cpp:156`, `:186`, `:285`). Any malformed `.dsp` yields a half-built tree with a NULL `ionode_`, which feeds straight into #2 and #4.
4. **NULL `ionode_` deref.** `thSynthTree`'s copy ctor (`:38`) and `buildSynthTree` (`:455`) assume a `io` declaration exists. Two of the three `loadTree` overloads don't validate it.
5. **NULL `plugin()` deref.** The grammar deliberately allows plugin-less nodes (`thinklang.yy:208`), but `thSynthTree.cpp:470` and `:241` call `->plugin()->...` unguarded.
6. **`gthALSAAudio(thSynth*, const char *device)` never sets `synth_`** (`gthALSAAudio.cpp:57`), then calls `SetFormat(synth_)`. Guaranteed crash with `-d alsa -o <device>`.
7. **`~gthALSAMidi` closes an uninitialized handle** (`gthALSAMidi.cpp:41`) whenever `snd_seq_open` failed. Crash on exit on any box without the sequencer.
8. **`thMidiChan::clearAll` iterates invalidated iterators** (`thMidiChan.cpp:113`): `delete i->second; notes_.erase(i); i++`. Reachable from the GUI Reset button.

### Tier 1 — the threading model

The audio thread runs the entire graph **with the mutex commented out** — `thSynth::process()` (`thSynth.cpp:412`, `:451`) and `getOutput()` (`:464`) have `pthread_mutex_lock` disabled, while the GUI/MIDI thread does `delete midiChannels_[n]` (`:323`), `realloc`s the channel array (`:312`), and inserts into `notes_` (`:368`) under that same mutex.

This is not fixable by uncommenting — the JACK callback must not block (the comment at `main.cpp:167` says as much). It needs a real design: build the new `thMidiChan` on the GUI thread, publish by atomic pointer swap, reclaim on the GUI thread after a grace period.

Related RT-safety violations inside the callback: `std::map::operator[]` allocating on every arg lookup (`thNode.h:44`), VLAs (`thMidiChan.cpp:195`), containers returned **by value** on hot paths (`thNode::args()`, `thSynthTree::nodes()` — `processHelper` copies a `std::list` per node per window), and sigc++ signal emission from the audio thread (`thMidiChan.cpp:254`).

### Tier 2 — GUI lifetime bugs

- `MainSynthWindow.cpp:609` passes `notebook_.get_current_page()` (which returns **-1** when empty) into `gthPatchManager::newPatch`, which has no bounds check and does `delete patches_[chan]`.
- `MidiMap` has five uninitialized member pointers (`MidiMap.h:111`) and holds raw `thArg*`s across patch reloads that free them (`MidiMap.cpp:559`, `:580`).
- `ArgTable.cpp:55` binds raw `thArg*` into slider callbacks; `newPatch` deletes the channel *before* emitting `signal_patches_changed`, leaving a window where sliders write to freed args.
- `~KeyboardWindow` (`KeyboardWindow.cpp:121`) `delete`s two `Gtk::manage`d adjustments.
- Neither `gthALSAAudio` nor `gthALSAMidi` derives from `sigc::trackable`, so their `mem_fun` connections are never auto-disconnected.
- The detached ALSA thread (`gthALSAAudio.cpp:52`) outlives the object it references.

### Tier 3 — off-by-ones and unchecked input

Off-by-one array growth in `thNode::copyArgs` (`thNode.cpp:173`, `>` should be `>=`) and `thPlugin::regArg` (`thPlugin.cpp:81`, same); `thArg::index_` never initialized in any of the six constructors while `buildArgMap` tests `index() < 0`; unchecked bounds in `thSynth::addNote`/`delNote` (`:350`, `:377`); `thMidiController::connections_[16][128]` indexed by raw MIDI bytes with no clamping; `DestroyMap` takes its map **by value** so `unloadPlugins()` leaves dangling pointers; `strchr` result written through unchecked in `gthPatchfile.cpp:202`; `getenv("HOME")` unchecked in `gthPrefs.cpp:49`; `exit(1)` inside the parser when a plugin fails to load (`thinklang.yy:192`).

And in `main.cpp:156`, the JACK callback `memcpy`s `getWindowlen()` floats into a buffer sized `nframes` — a port-buffer overrun whenever JACK's period is smaller than the synth window.

### Recommended first move

Build once with `-fsanitize=address,undefined` and run under `helgrind`. ASan will fire on item #1 at the first patch reload and will confirm or kill most of this list in an afternoon.

## 4. Visual editor

### 4a. DSP node editor

The format is already a graph, so the work is mostly in three places:

**The blocker: ports have no direction.** `plugin->regArg("a")` and `plugin->regArg("out")` are the identical call — nothing in the plugin API says which args are inputs and which are outputs, or what their ranges or meanings are. An editor needs that. Options:

- **Extend the plugin API** (bump `MODULE_IFACE_VER` to 5): `regArg(name, thPlugin::IN|OUT|INOUT, desc, min, max, default)`. Touches all 66 plugins mechanically but gives the editor real port typing, tooltips, and sane default ranges. This is the right answer.
- Sidecar metadata files per plugin. Less invasive, goes stale.
- Name heuristics (`out*`, `play`, `position` are outputs). Fine for a prototype, wrong eventually.

**Round-tripping.** The parser discards comments and formatting, so an editor that writes `.dsp` back will reflow the file. Node positions need somewhere to live — a structured comment (`# @layout node x y`) keeps the format backward-compatible with the existing parser and every checked-in `.dsp`.

**Host.** Three routes, in rough order of effort:

- *Web app.* A standalone editor (React + a node-graph lib) reading/writing `.dsp` over a small local server, or purely as a file-in/file-out tool. Fastest to a good UI, decoupled from the gtkmm mess, and gives a natural home for a JSON schema of the plugin metadata. Costs a second parser (or expose the bison one via a small CLI that emits JSON).
- *New gtkmm canvas widget in-app.* Live editing against the running synth — hear changes immediately, which is the real payoff. But it's built on the gtkmm-2.4 liability and cairo hand-drawing.
- *Port GUI to gtkmm-3/4 first, then build the canvas there.* Highest total cost, best endpoint.

### 4b. Patch editor

Worth clarifying what you want here, because `.patch` files aren't graphs — they're a `.dsp` reference plus flat `@chanarg` overrides, and `ArgTable` already renders them as sliders. Plausible readings: a proper preset browser/librarian (tags, categories, A/B compare, the `info` fields as real metadata); a nicer knob/panel layout editor where the `.dsp` author positions widgets; or unified editing where you tweak the graph and the preset side by side.

## 5. Suggested sequencing

1. ~~**Tier 0 crashes.**~~ Done — see section 6.
2. **Finish the clean build.** `char *desc` sweep across ~66 plugins, drop `-ffast-math` from the global flags, fix `configure.ac`'s obsolete macros (`AC_HELP_STRING`, `AC_TYPE_SIGNAL`), delete or fix `plugins/test/test.cpp`.
3. **Tier 2/3 crashes.** The GUI lifetime bugs, mostly mechanical.
4. **Port the GUI to gtkmm-3.** Decided; it gates where the node editor lives.
5. **Plugin API v5** with port direction and metadata. Unblocks the editor and improves the existing arg UI for free.
6. **Threading rework.** The largest single piece; needed before the synth is trustworthy under live editing.
7. **Node editor.**

## 6. What's been fixed

Everything below is on `master` as uncommitted changes. The tree builds clean
(GCC 16, C++17, `make -j8`, exit 0) and all 92 shipped `.dsp` files run through
the graph under ASan + UBSan with **zero** sanitizer reports.

### Verifying

`scripts/dspcheck.cpp` is a new headless harness: it loads each `.dsp` onto a
channel, plays a chord, overruns the polyphony limit, releases, reloads onto the
same channel (the patch-switch path), and tears down — no audio backend, no GUI.

```sh
./configure --enable-debug \
    CXXFLAGS="-g -O0 -fsanitize=address,undefined" \
    LDFLAGS="-fsanitize=address,undefined"
make -j8 && make -C scripts asan
LD_LIBRARY_PATH=libthink ASAN_OPTIONS=detect_leaks=0 \
    scripts/dspcheck-asan -q -p plugins/ $(find dsp -name '*.dsp')
```

Exit status is the number of files that failed to load. Currently 11, all of
them DSPs referencing `input/wav`, `input/alsa`, or `misc/wlan` — plugins that
compile but are deliberately not in `PLUGIN_DIRS` (`wav.cpp`'s own description
string says `"Wav Input (BROKEN)"`). Those used to `exit(1)` out of the whole
process; they now fail the parse and get reported.

### Build system

The build was broken before any of this: `build.mk` ran `bison -d` and then
renamed whichever header bison produced, which stopped working at bison 3 —
the generated `.cpp` `#include`s the header under bison's own name, so the
rename left a dangling include. `--defines=` is now passed up front, with real
targets for `thinklang.cpp`/`thinklang.h` in `libthink/Makefile.in` and a
`.SECONDARY` so make stops deleting the generated sources every build.
`thPluginManager.cpp` was also missing `<string.h>` for `strerror`.

### Tier 0

| What | Where |
|---|---|
| `nodeindex_` never initialised, then `delete[]`'d | `thSynthTree.cpp` both ctors |
| `nodeindex_` allocated uninitialised, partially filled | now value-initialised, plus a bounds-checked `nodeAt()` used at every subscript |
| `YYPARSE()` result discarded | new `thSynth::finishParse()`, shared by all three `loadTree()` overloads |
| NULL `ionode_` dereferenced | copy ctor, `buildSynthTree`, `process`, `printIONode` |
| NULL `plugin()` dereferenced | `processHelper`, `buildSynthTreeHelper` |
| `gthALSAAudio(synth, device)` never set `synth_` | guaranteed crash with `-d alsa -o <device>` |
| `~gthALSAMidi` closed an uninitialised handle | plus `pfds_` was clobbered *after* `open_seq()` allocated it |
| `clearAll` erased through live iterators | `thMidiChan`; also left `noteorder_` full of freed pointers |

### The patch-load crash

`gthPatchfile.cpp` assigned `string::find()`'s result to an `unsigned int` in
two places. `find()` returns a 64-bit `size_t`; truncating `npos` to 32 bits
gives `0xFFFFFFFF`, which compares *unequal* to `npos` — so the "not found"
case entered the loop body and called `replace()` with a position of
4294967295. On 32-bit systems in 2005 `size_t` and `unsigned int` were the same
width and this worked.

`EKeyboard.patch` hit it on its first `info` line (`info author Leif Ames` —
hence "size 9"). Every patch with an `info` field whose value contains no
literal `\n` would have crashed, i.e. essentially all of them.

Both sites now use `string::size_type`. Fixed alongside, in the same function:
the unchecked `strchr` result that an `info` line with no third field wrote
through, `buffer[strlen(buffer)-1]` underflowing on an empty line, and a
`NULL+1` formed before the NULL check that guarded it.

`scripts/dspcheck` now also takes `.patch` files, so this is covered:

```sh
LD_LIBRARY_PATH=libthink scripts/dspcheck -q -p plugins/ \
    $(find patches -name '*.patch')
```

99 of 101 load clean under ASan + UBSan. The two failures are
`patches/pads/Rythmic.patch` and `Rythmic-2.patch`, which point at
`/usr/local/share//thinksynth/dsp/mfm03.dsp` — an absolute path to a DSP that
is not in the tree (there is an `mfm01.dsp`, no `mfm03`). Leftovers from the
"Don't use absolute paths for patch files" cleanup.

### Found by the sanitizers

These were not in the original audit — they only surfaced once the harness
existed.

- **Allocator mismatch in the parser.** `plugname` builds names with
  `new char[]`; the `nodes` rule freed them with `free()`. Heap corruption on
  every node with a plugin, i.e. constantly. (`thinklang.yy`)
- **A failed parse poisoned the next one.** `loadTree` reassigns `yyin`, but
  flex holds its own buffer — so the unread tail of a file that failed to parse
  was fed to the following `loadTree`, which then failed at "line 1" for no
  visible reason. `yyrestart()` now runs at the top of `YYPARSE`.
- **`osc/static`** allocated one float for its inter-window state and wrote two.
- **`delay/fir`** and **`delay/echo`** both used `>` where they needed `>=` when
  wrapping the ring buffer index, writing one float past the end. `echo` also
  indexed out of range on a negative delay value.
- **`filt/ink`, `filt/inkshape`** used `abs((int)accel)` on a float that has
  diverged — the cast is undefined before `abs(INT_MIN)` even gets a chance.

### Also fixed along the way

- `thArg::index_` was never initialised in any of the six constructors, while
  `buildArgMap` tested `index() < 0` on it — an indeterminate value that could
  feed an out-of-bounds write into `copyArgs`. Same for `thNode::id_`.
- Off-by-one array growth in `thNode::copyArgs` (`>` should have been `>=`).
- `thSynth::addNote`/`delNote` bounds checks were off by one; `thSynth::clearAll`
  walked a `calloc`'d array looking for a NULL terminator it does not have.
- `thSynthTree::getArg(nodename, argname)` looked up `name_` — the tree's own
  name — instead of `nodename`, so it always resolved the wrong node.
- `map::operator[]` replaced with `find()` in `thNode::getArg`,
  `thMidiChan::getArg`, and `thSynthTree::getChanArg`. The old form inserted a
  NULL on every miss, which both allocated on the audio thread and left NULLs
  behind for every iteration site to guard against.
- `thNode::args()`, `children()`, `parents()` and `thSynthTree::nodes()` now
  return const references. `processHelper` was copying a `std::list` per node
  per window on the audio thread.
- JACK callback `memcpy`'d `getWindowlen()` floats into an `nframes` buffer;
  now clamped and zero-padded.
- `snd_seq_event_input`'s return value was discarded and `ev` dereferenced
  regardless.
- The detached ALSA polling thread ran `while (1)` and outlived the object whose
  buffers it read; it is now joinable and reaped in the destructor. Both
  `gthALSAAudio` and `gthALSAMidi` derive from `sigc::trackable` so their slots
  disconnect.
- Parser: `exit(1)` on a plugin that would not load (a library killing the host
  process) is now a parse error; `@foo.min` before `@foo` no longer dereferences
  NULL; the greedy `\".*\"` string pattern no longer merges two strings on one
  line; `nil` no longer propagates an uninitialised float; several leaks closed.
- `channels` from a `.dsp` is now range-checked before it sizes an allocation.

### Tree ownership

Trees always went into `thSynth::treelist_`, keyed by the DSP's `name`
statement — except `name "..."` never actually called `setName()`, so *every*
tree was called `newmod` and they all collided. Meanwhile
`thMidiChan::assignChanArgPointers` caches raw `thArg*` into whichever tree it
is handed, and `thMidiNote` copy-constructs from it per note. So two channels
on the same `.dsp` silently overwrote each other's chanarg pointers, and
destroying either left the other dereferencing freed args.

Each `thMidiChan` now owns its tree outright and destroys it. `finishParse`
takes a `registerTree` flag: the per-channel overload passes false, the two
whole-synth overloads keep the old `treelist_` behaviour (nothing else owns
those, so a name collision there only leaks — and it now deletes the loser
rather than leaking it). `name` and `description` now set the tree's actual
name and description, so trees report as `TS-1` and `Hat 0` instead of `newmod`.

`dspcheck` covers this: it loads each `.dsp` on channel 0 *and* channel 1,
plays both, drops one, and keeps processing the survivor. Reverting the fix
makes ASan fire a use-after-free on that path immediately, so the test has
teeth.

The channel array is no longer resized either. It grew with
`calloc`/`memcpy`/`free` while the audio thread iterated it; MIDI has 16
channels by protocol and `gthPatchManager` tops out at `NUM_PATCHES` (16), so
`TH_MIDI_CHANNELS` slots are allocated once at construction. `removeChan` also
actually deletes the channel now — the `delete` had been commented out, so
every removed channel leaked its notes, args and tree.

### Leaks

`dspcheck` under LeakSanitizer is now clean across all 92 DSPs and all 101
patch files. Fixed to get there:

- `~thSynthTree` never freed `chanargs_` — every `@foo = ...` in every `.dsp`
  leaked a `thArg` per load. This was the bulk of it.
- `~thPlugin` never freed its registered arg names or the array holding them.
  (`regArg` also had the same `>` vs `>=` off-by-one as `thNode::copyArgs`.)
- `~thSynth` never destroyed its channels; `~gthPatchManager` never freed the
  `patches_` array.
- `gthPatchManager::parse` built a `new string*[]` of individually `new`ed
  strings and freed it on no path at all, including its three `goto owned`
  exits. Now a `vector<string>`, which cleans up even when the goto jumps out
  of the block.
- Parser rules for `io`, `name`, `author` and `description` never freed their
  lexer tokens.

### Known-unfixed, deliberately

- **The threading model.** `thSynth::process()` still runs with its mutex
  commented out. See below.
- **GUI lifetime bugs** (Tier 2). Untouched, pending the gtkmm-3 port.

## 7. The threading fix — design

The tree-ownership half of this is done. The concurrency half is a design
change, so here is the shape of it before any code moves.

### What actually races

There are exactly two threads. The audio thread is JACK's RT callback
(`playback_callback` → `process_synth` → `thSynth::process`), or the detached
ALSA polling thread in ALSA mode. *Everything else* — MIDI events (they arrive
via `Glib::signal_io`, so they land on the GUI thread), the on-screen keyboard,
patch load/unload, preference restore — is the GUI thread.

So it is single-producer / single-consumer, which is the easy case.

The GUI thread currently reaches into live graph state in five ways, all of
which the audio thread is concurrently walking:

| GUI operation | Races with |
|---|---|
| `addNote` | inserts into `notes_`/`noteorder_` while `process()` iterates them |
| `delNote` | mutates a note's `trigger` arg mid-window |
| `clearAll` | frees every note out from under the mix loop |
| `loadTree(file, chan, amp)` | destroys a `thMidiChan` the callback may be inside |
| `setChanArg` | deletes a `thArg` the graph still points at |

### The proposed shape

Two lock-free SPSC ring buffers, and one rule: **the audio thread becomes the
sole mutator of everything reachable from `midiChannels_`.**

- *Forward queue*, GUI → audio. The GUI does all the allocation — building a
  `thMidiNote` copies an entire synth tree, which must not happen on the RT
  thread — and enqueues a command holding the finished object: `NOTE_ON`,
  `NOTE_OFF`, `ALL_NOTES_OFF`, `SET_CHANNEL`, `SET_CHAN_ARG`. The audio thread
  drains the queue at the top of `process()` and applies each one, which is
  just pointer and container manipulation.
- *Retire queue*, audio → GUI. Whatever the audio thread displaces — the old
  channel, the stolen note, the replaced arg — it hands back. The GUI frees it.
  Because the audio thread only releases a pointer once it is genuinely
  unreachable, this needs no epoch counting or grace period.

`removeChan`, `loadTree` and `setChanArg` become "build the replacement, enqueue
the swap, free what comes back". No locks on either side.

### The part this does not solve

Slider moves. `ArgTable` binds a raw `thArg*` and calls `setValue()` on it
from the GUI thread while the audio thread reads the same arg — that is the
whole point of a parameter control, and routing every drag through the queue
would be silly. For a single float where `len_` is already 1, `setValue` does
not reallocate, so the worst case is a torn read of one float; that is what
most synths live with, and the honest fix is an atomic or a smoothed parameter
rather than a queue. The `setValue(float*, int)` overload *can* reallocate and
does need to go through the queue.

### Verification — `scripts/dspstress`

`dspcheck` is single-threaded and cannot see any of this, so there is now a
second harness. `dspstress` runs a synthetic audio thread calling `process()`
in a loop while the main thread does what the GUI thread does. Work is split
into levels so a report can be blamed on one kind of operation rather than on
"something concurrent", and each level runs in a forked child with a watchdog,
so a level that crashes or wedges does not take the run with it:

| Level | Exercises |
|---|---|
| 1 `notes` | `addNote` / `delNote` |
| 2 `clear` | + `clearAll` |
| 3 `chanargs` | + `setChanArg` and slider-style `setValue` |
| 4 `reload` | + `loadTree` onto a live channel, `removeChan` |

ThreadSanitizer cannot be combined with AddressSanitizer, so this needs its own
build tree:

```sh
./configure --enable-debug \
    CXXFLAGS="-g -O1 -fsanitize=thread" LDFLAGS="-fsanitize=thread"
make -j8 && make -C scripts tsan
LD_LIBRARY_PATH=libthink scripts/dspstress-tsan -p plugins/ dsp/ts1.dsp
```

Exit status is the number of levels that failed. The tool sets its own
`__tsan_default_options`, so a race fails the run without anyone having to
remember the environment variable.

### The static: integer wraparound on the output stage

**This was the one.** Not a race, and not the uninitialised buffers below —
those were real bugs found on the way, but they were not what you could hear.

`thSynth::process` sums every sounding note into one buffer with no headroom
management at all. Each voice contributes up to `TH_MAX`, scaled only by the
channel amplitude, so two voices reach roughly twice full scale and three about
three times. Measured on the shipped DSPs:

```
dsp/ts1.dsp
  1 voice:  peak  0.617   clipped   0.0%
  2 voices: peak  1.234   clipped   1.0%
  3 voices: peak  1.523   clipped  14.5%
  4 voices: peak  2.050   clipped  21.5%

dsp/anasync.dsp                                    (what EKeyboard.patch uses)
  1 voice:  peak  1.587   clipped   2.3%   <- already hot on one voice
  2 voices: peak  3.017   clipped  14.5%
```

That reached the output stage unclamped. The ALSA path does

```c
(signed short)(((float)inbuf[...] / TH_MAX) * 32767)
```

and converting an out-of-range float to an integer type is undefined. In
practice it wraps:

```
float 0.617 ->  20217 -> int16  20217     fine
float 1.234 ->  40434 -> int16 -25102     full-scale sign flip
float 1.523 ->  49904 -> int16 -15632
float 3.017 ->  98858 -> int16 -32214
```

So every sample past full scale became a near-full-scale discontinuity of the
*opposite* sign. That is the static, and it explains every part of the symptom:
loudest on the attack where the envelope peaks, gone by the sustain once the
signal drops back inside the rails, absent on one note for most DSPs, and worse
and longer-lasting with each extra voice held down. The JACK path had the same
problem more mildly -- raw floats handed to a port that expects -1..1.

`thClampSample()` (in `think.h`) now guards both paths. That converts
wraparound into ordinary hard clipping, which is a bug fix, not a design.

**Keeping the mix inside the rails is still open.** Clamping means `ts1.dsp`
distorts on 21% of samples with four voices held, and `anasync.dsp` clips on a
single note. The options are per-voice gain staging (divide by polyphony, or by
`sqrt` of it), a proper limiter on the output, or simply lowering the default
channel amplitude. `scripts/dsplevel` measures it:

```sh
LD_LIBRARY_PATH=libthink scripts/dsplevel -p plugins/ $(find dsp -name '*.dsp')
```

Its exit status is the number of DSPs that clip on a *single* voice — those are
the ones whose own gain is too hot, as opposed to polyphony summing.

### Uninitialised plugin state (found on the way, also real)

Chasing the races was the wrong tree. The giveaway was that the noise sits on a
note's **attack** and clears by the time it sustains — a race scatters noise
uniformly, so a symptom that tracks the envelope is state, not timing.

`thArg::allocate()` handed back raw `new float[]` memory. Plugins keep their
state in args — delay lines, filter history, oscillator phase — and read it
back before writing it; `delay/echo`, `delay/fir`, `filt/comb` and
`filt/allpass` all do. A note's args are copy-constructed from the channel's
prototype tree, where they hold a single placeholder value, so the **first
window of every note** resizes each of them from 1 to `windowlen` and the
plugin reads whatever was in that heap block. From the second window on, `len_`
already matches, `allocate()` returns the same buffer untouched, and it holds
real audio. Hence: a burst on the attack, silence on the sustain.

It also explains why this was not obvious originally. Freshly mapped pages
arrive from the kernel zero-filled, so on a quiet heap the garbage was usually
silence. On a busy one — more notes, more allocation churn — the block comes
back holding the previous note's samples at full scale.

`allocate()` now value-initialises (`new float[elements]()`). A resize
deliberately does not preserve the old contents: going from one value to a
whole window means the old value was a placeholder, and zero is the right
initial state for a delay line.

**This is not something AddressSanitizer can find** — it tracks addresses, not
initialisation. `dspcheck` now catches it by rendering the same note twice from
a fresh synth and comparing bitwise:

```
FAIL  dsp/anasync.dsp (non-deterministic output, first differing window 0
      -- a plugin is reading uninitialised state)
```

**19 of the 92 shipped DSPs failed that check before the fix, every one of them
differing at window 0.** Zero fail after it. (`renderNote` reseeds `rand()`, or
the twelve DSPs built on `osc::static` would show up as false positives.)

### The queue rework, and a bad measurement

The command queue works. With a synthetic audio thread hammered by GUI-side
note, parameter and patch changes, `dspstress` reports:

| Level | Before the queue | After |
|---|---|---|
| 1 `notes` | 280 races | **0** |
| 2 `clear` | 95 races, then hung | **0** |
| 3 `chanargs` | — | **0** |
| 4 `reload` | — | **0** |

Getting there required correcting a measurement error worth recording, because
it wasted a lot of effort and produced numbers this document previously
reported as fact.

`configure.ac` *assigned* `CXXFLAGS` rather than appending to it:

```sh
CXXFLAGS="-Wall -ffast-math"
```

So `./configure CXXFLAGS="-fsanitize=thread"` was silently discarded, and every
"ThreadSanitizer" build had **libthink and the plugins uninstrumented**. Only
the harness itself carried the flag. TSan still saw malloc/free and pthread
calls through its interceptors, so it still produced reports -- but it could
not see the ring's atomics, so it had no way to derive the happens-before edge
between the two threads. That is precisely why every remaining report had the
same shape: *main allocated this block, audio wrote it, with no ordering*.

It also explains why a mutex around the queue handoff silenced everything
(pthread calls are intercepted regardless of instrumentation) and why explicit
`__tsan_acquire`/`__tsan_release` annotations did too (they are library calls
into libtsan). Both "fixes" were supplying an edge that the invisible atomics
already established. The ring was correct all along, exactly as its standalone
test said.

`configure.ac` now saves the user's `CXXFLAGS` up front and re-appends them
last, after the `-O2`/`-g3` logic, so anything passed to configure wins. With
that in place and libthink genuinely instrumented, the ring's own atomics are
enough and the annotations were removed.

Two real races did surface once TSan could see properly, both in
`thArg::setValue`:

- It did `values_ = allocate(1)` unconditionally. Even when `allocate` returns
  the same pointer, that is an 8-byte non-atomic write to a member the audio
  thread reads in `getBuffer()`. It now writes only the float, leaving
  `values_` alone, in the no-reallocation case.
- `thSynth::setChanArg` was queueing every change, including plain value
  updates, which is what broke the GUI reading its own writes back. It now
  applies scalar-to-scalar updates in place -- one relaxed atomic store, no
  reallocation, immediately visible -- and queues only genuine replacements.

### Superseded: where the queue rework got to (measured wrong)

An earlier revision of this document reported the queue taking level 1 "from
52 races to 15", and described 15 remaining races as unexplained. Both numbers
came from the uninstrumented builds described above and should be disregarded.
The experiments run against them -- mutex around the drain, retirement
disabled, annotations -- were all measuring the same artefact.

### What the harness found originally

Against the pre-queue code (commit `64ff0b3`), with libthink properly
instrumented, **every level fails**:

- **Level 1** — 280 race reports in well under a second, from
  `addNote`/`delNote` alone.
- **Level 2** — 95 reports, then hangs: `clearAll` concurrent with `process()`
  corrupts the note containers badly enough to spin forever.
- **Level 4** — SEGV. Reloading a patch on a channel the callback is inside.

The level 1 report is the interesting one, because it explains the audible
symptom — static when two notes sound together:

```
Write of size 8 by thread T1:
    thArg::allocate()            thArg.cpp:171
    thPlugin::fire()             thPlugin.cpp:74

Previous write of size 8 at the same address by main thread
                                            (mutexes: write M0):
    thArg::thArg(const thArg *)  thArg.cpp:116
```

Both threads writing **the same address**. The GUI thread is inside `addNote`,
where building a `thMidiNote` copy-constructs an entire synth tree and
allocates a `values_` buffer for every arg in it. The audio thread is inside a
plugin calling `allocate()` on an arg, which does `delete[]` then `new`. They
are fighting over the same heap, and the loser's buffer is what gets mixed into
the output. That is the static.

Note the `(mutexes: write M0)` on the GUI thread and its absence on the audio
thread: TSan is pointing straight at the commented-out lock in
`thSynth::process()`.

This also explains why it did not show up originally. On a single-core machine
the GUI thread's `addNote` almost always ran to completion inside one scheduler
quantum, so the two threads interleaved but never genuinely overlapped. Two
notes at once makes it worse because it means two tree copies — hundreds of
allocations each — landing back to back while the audio thread runs.

As with the tree-ownership fix, the harness was confirmed to fail before it is
trusted to pass.

### If you have thinksynth installed in /usr/local

**`lib_major` is now 5.** The queue rework changed `thSynth`'s layout (two ring
buffers and a second channel array) and several signatures, which is an ABI
break by `configure.ac`'s own criteria.

This matters more than it sounds. Before the bump, a binary built against the
new headers still resolved `libthink.so.4.0` from `/usr/local/lib` at run time,
because the soname had not changed. It linked without complaint and then read
every header-inlined accessor at the wrong offset — `thSynth::getChannel`
returning garbage, the arg tables coming up empty, the keyboard's channel
spinner showing values like `-733809408`. Silent corruption, no diagnostic.

With the soname at 5 the failure is loud instead: `libthink.so.5.0 => not
found`. Run from the build tree with

```sh
LD_LIBRARY_PATH=libthink ./src/thinksynth ...
```

or `make install` first. The stale `/usr/local/lib/libthink.so.4.0` can now
sit there harmlessly.

### Also worth noting

Nothing here makes the audio thread hard-RT-safe, and this document should not
pretend otherwise. `process()` still builds `std::string`s in its inner loop
(`argname = OUTPUTPREFIX; argname += ...`), uses VLAs sized by window length,
and inserts into `std::map`. The goal above is *race freedom* — no torn
containers, no use-after-free. RT purity is the separate item the TODO file has
always listed, and it wants the same command-queue plumbing underneath it.
