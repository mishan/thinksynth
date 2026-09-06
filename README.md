thinksynth
==========

thinksynth builds sounds out of a graph of DSP nodes — oscillators,
filters, envelopes, delays and arithmetic — described in a small
language and rendered in real time. Patches assign those graphs to MIDI
channels, so one instrument can layer several voices.

Every node is a plugin, and the set that ships covers subtractive and
FM synthesis, resonators, waveshaping and a range of filters. New ones
are ordinary shared libraries dropped into the plugin directory.

Building
--------

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/src/thinksynth
```

On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build bison flex pkg-config \
    libgtkmm-4.0-dev libsigc++-3.0-dev libasound2-dev
```

Optional, and each one buys something specific:

```sh
sudo apt install libjack-jackd2-dev libpulse-dev \
    adwaita-icon-theme librsvg2-common xvfb
```

`libasound2-dev` and `libjack-jackd2-dev` are there for **RtAudio**, not for
thinksynth: no source in this tree includes an ALSA or a JACK header. RtAudio
and RtMidi are the single audio and MIDI API the whole program talks to, and
they are normally *built from source* as part of the build — a system copy is
used only when pkg-config reports 6.0.0 or newer, which Ubuntu does not ship
(24.04 has no `librtaudio-dev` at all and 22.04 has 5.2, which is a different
API). So building RtAudio's Linux backends is part of building thinksynth, and
those headers are what it needs.

Which is also why the JACK and PulseAudio ones are optional: CMake probes for
them and compiles those backends in only if they are there. ALSA is always on.
The configure summary says what you ended up with, including whether RtAudio
came from the system or from source.

The rest are runtime or test-time. `adwaita-icon-theme` and `librsvg2-common`
give the icons their intended look, and `xvfb` is needed by `ctest` because one
gate builds real widgets.

Useful options:

| | |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | unoptimised, with symbols |
| `-DTHINK_ENABLE_DEBUG=ON` | the tree's own debug logging |
| `-DTHINK_SANITIZE=address,undefined` | applied to libthink and the plugins too, not just the harnesses |
| `-DTHINK_SANITIZE=thread` | needs its own build tree; cannot be combined with the above |

Running an uninstalled build works: thinksynth tries `./plugins` first and then
the directories around its own binary, and `THINK_PLUGIN_PATH` overrides both.

Audio and MIDI
--------------

Audio goes through RtAudio and MIDI through RtMidi, so the same build talks to
ALSA, JACK, CoreAudio, WASAPI, CoreMIDI and WinMM without a per-backend code
path.

```sh
thinksynth -d rtaudio          # the default
thinksynth -d jack             # JACK directly
thinksynth -d none             # no audio device; useful headless
```

The audio API and device are remembered in `thinkrc`, and the preferences
dialogue lists what the machine actually has — RtAudio enumerates devices, so
there is nothing to type.

`thinkrc` lives under the platform's config directory —
`~/.config/thinksynth/thinkrc` on Linux, `~/Library/Application
Support/thinksynth/thinkrc` on macOS, `%LOCALAPPDATA%\thinksynth\thinkrc` on
Windows. A `~/.thinkrc` from an older version is still read if no current one
exists, but is never written back to.

First run
---------

There is nothing to set up. With no configuration file anywhere, thinksynth
writes one and starts with four channels already loaded:

```
channel 0,leads/SuperRes.patch,30
channel 1,bass/FunkMachine.patch,30
channel 2,organs/Organ1.patch,30
channel 3,pads/SynString.patch,30
```

So the on-screen keyboard makes a sound immediately, and the channel spinner
moves between four different ones. Edit or delete lines to change that; delete
the whole file to get the defaults back.

The patches are named relatively and looked up the same way DSPs are, which is
what lets the file survive the install moving — a `.app`, a Windows zip and a
Flatpak all live somewhere the build never knew about. `THINK_PATCH_PATH`
overrides where they are searched for.

On Windows, launching thinksynth does not open a terminal alongside it — it is
a GUI-subsystem program. Run it *from* a terminal and it prints there anyway,
so `-h`, `-G` and every diagnostic still work when you want them; redirection
(`thinksynth.exe -h > log.txt`) works whether or not a terminal is involved.

For MIDI, connect an external sequencer or keyboard to thinksynth's input port
using whatever your platform uses for that (`aconnect` or a patchbay on Linux,
Audio MIDI Setup on macOS). Assign a DSP to each channel the MIDI uses and turn
up the amplitudes. The on-screen keyboard works without any of this.

If you want JACK on Linux, start `jackd` before thinksynth — RtAudio will use
the running server.

Composing
---------

thinksynth also writes music. A `.gen` file describes a piece the way a
`.dsp` describes a sound: as a small graph, in the same language, with the
same scanner. Composer plugins — tape loops, Euclidean rhythms, L-systems,
Markov chains, cellular automata, genetic algorithms — run in chains, each
stage hearing only the one before it, and a sink at the end says where the
notes go.

```
instrument pad {
    dsp  "amb01.dsp";
    a    = 900 ms;
};

chain loop_ab3 {
    stage src gen::eno_line {
        notes = "Ab3"; period = 19.4 s; jitter = 1.5 s;
        prob = @density; hold = 6 s;
    };
    sink { instrument = pad; };
};
```

A piece carries its own instruments, so one file is the whole thing: open
`gen/airports.gen` from the Composer window (**☰ → Open**), press **Play**,
and seven loops whose periods share no factor start and never repeat. A
`@knob` declared in the piece is a live slider, and one slider can drive a
stage's density and the instrument's own filter at once.

The line between composing and synthesis runs both ways. A DSP node can be a
stage — `breath.gen` puts an `osc::simple` and an `env::adsr` in a chain,
running at the composer's rate, shaping a line's density over twenty seconds
with the same plugins a patch is built from. And a composer can reach back
into the instrument: `reshape.gen` rebuilds a channel around a different
`.dsp` every forty seconds and moves constants the patch never declared,
while the notes stay exactly the same. Everything you hear moving is the
instrument underneath them.

Time carries its unit. `period = 19.4 s` is a free-running loop;
`period = 4 beats` is clocked and moves with the tempo. A piece with a
`seed` replays identically, and the build proves it: `scripts/gencheck`
loads every shipped piece, renders it twice through a virtual clock, and
diffs the two note streams byte for byte.

Fourteen pieces ship, each built around one idea and meant to be read as
well as heard — [`gen/README.md`](gen/README.md) is the index.
[`GEN_FORMAT.md`](GEN_FORMAT.md) is the language, and
[`UNIFICATION.md`](UNIFICATION.md) is where the two languages are going,
and why they keep their `node` and `stage` keywords apart on purpose.

Documentation
-------------

| | |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | the layers, key classes, threading model, audio and MIDI paths |
| [DSP_FORMAT.md](DSP_FORMAT.md) | the `.dsp` and `.patch` formats, and the rules for writing them |
| [AUDIO.md](AUDIO.md) | the output stage: clamping, gain staging, arg initialisation, the harnesses |
| [NODE_EDITOR.md](NODE_EDITOR.md) | the visual editor's model, behaviour and layout |
| [VISUALIZERS.md](VISUALIZERS.md) | writing a visual module, and how probes work |
| [PORTING.md](PORTING.md) | macOS and Windows: decisions, build system, CI, traps |
| [PACKAGING.md](PACKAGING.md) | the three install layouts, dependency closure, GTK bundling, Flatpak |
| [TODO](TODO) | what is left |
