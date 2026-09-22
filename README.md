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

Twenty-six pieces ship, each built around one idea and meant to be read as
well as heard — [`gen/README.md`](gen/README.md) is the index.
[`docs/GEN_FORMAT.md`](docs/GEN_FORMAT.md) is the language, and
[`docs/UNIFICATION.md`](docs/UNIFICATION.md) is where the two languages are going,
and why they keep their `node` and `stage` keywords apart on purpose.

In a browser
------------

The same engine compiled to WebAssembly and playing in a tab. Two things to
play: a patch — one `.dsp` — and a piece, a `.gen` composed as it plays,
with the knobs it declares as sliders and a piano roll of what it
delivered. Either is played from the keys on screen or from the computer
keyboard, and the page works on a phone: the keys take as many octaves as
the screen has room for, several fingers at once, and a phone held sideways
keeps them under the thumbs while the rest of the page scrolls past.

All of it is one AudioWorklet: libthink, all 62 DSP plugins, all 16
composers and the composer scheduler in one wasm module, with the transport
stepped by the audio clock itself rather than by a timer. So a page and a
static file server are the whole of what it takes to hear it.
[docs/JAM.md](docs/JAM.md) is where this is going: several people playing one piece,
each browser rendering it locally.

Try it at <https://mishan.github.io/thinksynth/> — master's build, which CI
publishes once the wasm gates pass on it.

Emscripten builds it, at a pinned version — the comparison against the
native build is only as repeatable as the compiler on the wasm side:

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.9 && ~/emsdk/emsdk activate 6.0.9
```

On Debian or Ubuntu this asks for rather less than the desktop build does —
no gtkmm, no cairo, no pkg-config:

```sh
sudo apt install git cmake ninja-build bison flex python3 curl xz-utils
```

Then build the site and serve it. The room page (below) has an editor
and a CRDT in it, which come from npm and are bundled at build time, so
`npm ci` comes once before the configure:

```sh
source ~/emsdk/emsdk_env.sh
(cd wasm/web && npm ci)
emcmake cmake -S wasm/web -B build-web -G Ninja
cmake --build build-web -j
node wasm/web/serve.mjs            # http://localhost:8080/
```

Press **Start** and play — touch or click the keys, or use the computer
keyboard: `Z` to `/` is an octave and a bit from C, `Q` to `P` the octave
above, and `-` and `=` move both, as do the arrows beside the keys. Open
**Patch source** to edit the `.dsp`, and **Load** to hear the change.

Switch **Play** to *a piece* for the other half: pick one of the shipped
`.gen` files, **Load**, **Play**. The sliders are whatever knobs the piece
declared and move it as it runs; the roll is what the scheduler has
delivered, a colour per channel. The keyboard still plays, into whatever
chains the piece routed `input midi` to.

Under the roll is a line for each channel the piece touches. A piece that
carries its own instruments has aimed them itself and says so. One whose
sinks just name channels — `fern.gen` asks for something plucked and a soft
pad — is asking the reader to aim them, and the page fills each from the
same patch the desktop's first run puts there, so the piece sounds without
being set up; the menu on its line is every shipped `.patch` and `.dsp` if
you want something else. What a channel sounds like is the piece's to say
and, where the piece is silent, the defaults' — never what the page did a
moment ago.

It has to be served, and to localhost: a worklet module will not load from
a `file://` path, and a browser counts https and localhost as secure
contexts and nothing else. To play it from another machine, forward the
port — `ssh -L 8080:localhost:8080 host` — rather than serving on 0.0.0.0,
which its browser will not trust.

`node` comes with the emsdk, on `PATH` after `emsdk_env.sh`; Debian's
`nodejs` package does as well. Configuring fetches sigc++, so the first
run needs the network.

### Putting it somewhere else

The build directory is the whole site and `serve.mjs` serves it where it
stands. To host it anywhere else, package it first:

```sh
cmake --build build-web --target dist
```

which writes `build-web/dist/` — the files a server needs and no others,
around 4 MB, every file 644 and every directory 755 — after removing
whatever an earlier `dist` left, so a stale file cannot ride along. Copy
*that*. For a destination of your own, `cmake --install build-web --prefix
DIR` is the same rules with the prefix as the site root, putting
`index.html` at `DIR/index.html`.

Not a copy of the build directory: it carries the object files and CMake's
own state, and copying keeps each file's mode from the source tree, so a
`.dsp` that is 640 here is 640 on the server and a 403 in the browser —
which the page then loads as an HTML error page and fails to parse.

### Playing together

`jam.html` is the same synth with a room around it: several people, one
piece, each browser rendering the whole of it. The piece's text is shared
and edited together, with everyone's cursors; Play starts every peer's
transport at one agreed moment; a knob moved anywhere moves everywhere at
the same point in the piece; keys play into the seat you took. What
crosses the network is the score, never the audio.
[docs/JAM.md](docs/JAM.md) is the design.

It needs a relay: one small server that holds the document, answers the
clock, and introduces the peers to each other. Run it beside the site:

```sh
node wasm/web/relay.mjs                                # ws://0.0.0.0:8787
node wasm/web/serve.mjs --relay ws://localhost:8787    # http://localhost:8080/
```

and open `http://localhost:8080/jam.html`, pick a room and a name, Join,
Start, take a seat, Play. A second tab in the same room is a second peer.
For a second machine on the LAN, forward the site's port as above and
open it as `localhost`; the relay needs no secure context, so give it
the first machine's address: `?relay=ws://192.168.1.10:8787` on the URL,
or `--relay` to that machine's `serve.mjs`. A new room is seeded with
`gen/airports.gen`; `?piece=ebb.gen` seeds it with another.

The numbers panel shows what the clocks think -- the relay's round trip
and the spread of the offset, the audio clock's residual -- and how many
commands arrived after their time. **Download the tape** on two peers
after a play, and diff them: they should be the same file.

`wasm/` is the other Emscripten build, the same engine under Node:
`wasm/genwav.mjs` renders a `.gen` the way `scripts/genwav` does, and
`wasm/compare.mjs` holds the two builds against each other piece by piece.

The browser build has its own checks. `wasm/web/check.mjs` plays every
shipped patch through the module. `wasm/web/piececheck.mjs` composes every
seeded piece in it — at 48 kHz and 44.1, in windows of 256 and of 128 — and
diffs each tape against the one `genwav.mjs` delivers under Node, where the
plugins are dlopened rather than linked in and the transport is stepped by a
fixed clock in windows of 1024. All four have to agree, because what a piece
composes is a function of the file and the seed and of nothing else. It then
plays every shipped piece the way the page plays it, defaults and all, and
asks for a peak: a tape says what was composed and not whether any of it was
audible, and a piece that is silent under the page's defaults fails the
build.
`wasm/web/browsertest.mjs` runs both of those through the worklet in
Chromium and Firefox, `wasm/web/pagetest.mjs` drives the solo page's own
keys and knobs in Chromium, and `wasm/web/bench.mjs` reports what one
128-frame quantum costs with a piece running and a chord held down. For
the room:
`wasm/web/protocoltest.mjs` runs two peers in one process at different
windows and rates over a simulated network and holds their tapes against
each other and against `genwav.mjs`'s under the same commands;
`wasm/web/relaytest.mjs` drives the relay from Node; and
`wasm/web/jamtest.mjs` puts a Chromium page and a Firefox page in one
room on a relay and does the same comparison live.

Documentation
-------------

| | |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | the layers, key classes, threading model, audio and MIDI paths |
| [docs/DSP_FORMAT.md](docs/DSP_FORMAT.md) | the `.dsp` and `.patch` formats, and the rules for writing them |
| [docs/GEN_FORMAT.md](docs/GEN_FORMAT.md) | the `.gen` format: chains, sinks, instruments, knobs, the arrangement |
| [docs/NODES.md](docs/NODES.md) | every node's args: direction, default, range, units. Generated from the plugins |
| [docs/AUDIO.md](docs/AUDIO.md) | the output stage: clamping, gain staging, arg initialisation, the harnesses |
| [docs/NODE_EDITOR.md](docs/NODE_EDITOR.md) | the visual editor's model, behaviour and layout |
| [docs/VISUALIZERS.md](docs/VISUALIZERS.md) | writing a visual module, and how probes work |
| [docs/PORTING.md](docs/PORTING.md) | macOS and Windows: decisions, build system, CI, traps |
| [docs/JAM.md](docs/JAM.md) | playing together in a browser: the plan, milestones and risks |
| [docs/JAM_BACKLOG.md](docs/JAM_BACKLOG.md) | what comes after those milestones, and the decisions to make early |
| [docs/UNIFICATION.md](docs/UNIFICATION.md) | uniting the two languages: what has landed and what is left |
| [docs/PACKAGING.md](docs/PACKAGING.md) | the three install layouts, dependency closure, GTK bundling, Flatpak |
| [TODO](TODO) | what is left |
