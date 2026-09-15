# Playing together in a browser — a plan

One piece, several people, each browser rendering the whole thing locally.
The score crosses the network; the audio never does.

This is the plan for getting there from the tree as it stands. It leans on
two properties the tree already has, and would be a different and much
harder project without either: a seeded piece replays identically
([GEN_FORMAT.md](GEN_FORMAT.md), `gencheck`), and the engine has exactly
two threads talking through a lock-free command queue
([ARCHITECTURE.md](ARCHITECTURE.md#threading)), which is the shape a browser
imposes anyway.

## 0. Where it stands

The risk-retiring step is done. `wasm/` builds libthink, the composer host
and every plugin with Emscripten, and `wasm/genwav.mjs` renders a `.gen`
under Node the way `scripts/genwav` does natively. `wasm/compare.mjs` runs
both over every seeded piece and diffs tapes, WAVs, summaries and exit
statuses.

With the determinism fix in (tie order in the scheduler's heap and in
`breed`, `evolve` and `arp`; `libthink/thcRandom.h`, a portable
PRNG-to-distribution layer, for `swap` and `breed`; `osc::static` off
`rand()`):

| | |
|---|---|
| seeded pieces compared | 13 |
| tapes identical | 13 |
| WAVs byte-identical | 11 |
| WAVs off by 1 LSB in a handful of samples | 2 (`orrery`, `round`) |

The two stragglers are glibc against musl in libm, and they matter less than
they look. What the jam needs identical across peers is the *tape*, the
event stream, because that is what every peer computes independently. Audio
is rendered locally and never compared. And between two wasm peers the libm
is the same musl compiled into the same module, so the last-bit difference
that exists between native and wasm does not exist between one browser and
another. WebAssembly float arithmetic is IEEE-deterministic across engines
apart from NaN payloads, which nothing here depends on.

Where M0 stands:

- The determinism fix is in the working tree, done properly rather than as
  the experiment's patch: the scheduler's tie-break, `thcRandom.h`, stable
  sorts, and `osc::static` on a generator of its own that restarts whenever
  a synth loads the plugin, so `dspcheck`'s render-twice check still holds
  without `srand`. All 19 ctest gates pass with it in.
- `.github/workflows/ci.yml` has a `wasm` job: native `genwav` and the
  plugins, emsdk 6.0.9 (cached), the wasm build, then
  `node wasm/compare.mjs --lsb 1`. `--lsb 1` holds tapes, summaries and exit
  statuses to identical and lets a WAV sample differ by one step, which is
  the libm straggler above. Every step has been run end to end on a fresh
  copy of the tree; the job has not yet run on a runner.
- All of it is on the `wasm-parity` branch: `scripts/genwav` and the
  pieces it renders, then the fix, then `wasm/` and the job. M0 is done
  when that branch's `wasm` job is green on a runner.

### M1, so far

On the `jam-m1` branch, which starts where `wasm-parity` ends:

- `wasm/web/` builds the browser module: libthink and all 62 DSP plugins
  linked into one 268 KB wasm file. Each plugin is compiled in a namespace
  of its own and listed in a table the static branch of `thDynLib` reads,
  so nothing above that seam changed. The page (`index.html`, `main.js`)
  has the `.dsp` in a text box, the computer keyboard as the keyboard, and
  the latency the browser reports; `worklet.js` runs the synth at a window
  of 256.
- Commands carry the frame they apply at and land at the start of the
  window it falls in. A note then sounds from the *next* window -- the
  engine's onset, the desktop's too -- so a key costs one to two windows on
  top of the output latency: 5–11 ms at 256 and 48 kHz.
- `scripts/dspab -B 256` over the corpus: 78 of the 81 DSPs that load
  render the same at 256 as at 1024. The three that do not --
  `noargs/dfb`, `noargs/smoothie`, `old/randompw` -- are exactly the three
  loadable DSPs whose graph has a cycle. The engine breaks a cycle by
  letting one node read the previous window, so the loop's delay *is* the
  window: 23 ms at 1024, 5.3 ms at 256. Understood, and no plugin read the
  window length as a constant.
- `wasm/web/check.mjs` plays every shipped patch through the module from
  Node. `wasm/web/browsertest.mjs` renders a phrase through the worklet in
  headless Chromium and Firefox and matches the module run directly,
  sample for sample; the page itself starts, loads, takes keys and reports
  latency in both. The CI `wasm` job runs both.
- What is left for done: playing it on real hardware, in Chrome and
  Firefox, on Linux, macOS and Windows. A headless browser has no sound
  card to hear.

## 1. The three kinds of state

Everything a peer can know about a session is one of three things, and each
has its own delivery, its own conflict rule and its own latency tolerance.
Keeping them apart is most of the design.

| | Document | Transport | Gestures |
|---|---|---|---|
| what | the piece: `.gen` text, the `.dsp` files it names, presets | tempo, seed, playing or stopped, origin time | knob moves, hand-played notes |
| changes | rarely, by editing | rarely, by pressing play or changing tempo | constantly |
| ordering | matters; must converge | matters; tiny | latest wins, or scheduled by beat |
| delivery | CRDT (Yjs) over a relay | reliable, via the same relay | WebRTC data channel, unordered, no retransmit |
| latency tolerance | seconds | seconds, but must apply at an agreed beat | milliseconds |

What is *not* on the list is the generated material. Every stage of every
chain runs on every peer, from the same seed, against the same transport, so
a note that `euclid` emits on my machine is the same note it emits on yours
at the same beat. Nothing about it is transmitted. The bandwidth of a session
is the bandwidth of people typing and turning knobs.

Three consequences fall out:

- **Late join is a fast-forward.** A peer arriving at bar 40 fetches the
  document and the transport state and runs the scheduler through its
  virtual clock from zero to now, delivering nothing to the synth until it
  catches up. `gencheck` already does exactly this run in a few seconds for a
  three-minute piece.
- **Determinism is a correctness requirement, not tidiness.** Any composer
  whose output can depend on the platform is a composer that will silently
  desynchronise two peers. `compare.mjs` is the gate for native-versus-wasm;
  section 7 adds the wasm-versus-wasm one across browsers.
- **The audio thread sees no network.** The worklet gets the same five
  commands it gets today. The network lives entirely on the main thread, in
  front of the command queue, where the GUI lives now.

## 2. Latency, and where it goes

Musicians feel lag from about 20–30 ms one way and ensembles drift above
roughly 50 ms. One-way fiber is about 5 ms per 1000 km, so peers in one metro
see 5–15 ms and a coast-to-coast pair sees 35–40 ms before any software is
involved. That is the physics and no design changes it.

Generated material has no network latency, because it is not transmitted.
Knob moves tolerate whatever they get. So the only thing on the clock is a
hand-played note, and the budget for one is:

```
network one way        5–40 ms     distance, nothing to do about it
output buffer          5–20 ms     AudioContext, platform dependent
synth window           windowlen / rate
scheduling lookahead   one worklet quantum or so
```

The synth window is the one the tree controls. `TH_DEFAULT_WINDOW_LENGTH` is
1024, which is 23 ms at 44.1 kHz on its own, and a note-on applies at the top
of the next `process()`. The web build wants 128 or 256. The ring between
`process()` and the device already handles a device period that is not the
window, so this is a number, not a rework — but it is a number a plugin might
have assumed. `scripts/dspab` over the corpus at 256 against 1024 is the
check, and anything that moves is a plugin that read the window length as a
constant.

Then three ways to play, chosen per seat, with the measured round trip shown
next to the choice so nobody has to guess:

- **Direct.** Notes are scheduled on arrival. Right for peers in one city.
- **Quantised.** The seat's live input feeds a chain with an
  `xform::quantize` stage before its sink. A note lands on the grid slot it
  was played into, on every peer, and jitter shorter than the grid vanishes.
  With a 16th at 120 bpm that is 125 ms of tolerance. This is free: a live
  note is an event with a time entering a chain, which `arp` already
  consumes, and `thcChain::inputMidi` already exists.
- **Play-ahead.** Every seat hears every *other* seat one beat or one bar
  late, NINJAM's trick. Coherent against the grid, useless for
  call-and-response, and the only thing that works across an ocean.

## 3. Architecture in the browser

```
  editor              transport            input
  CodeMirror + Yjs    play/tempo/seed      keys, Web MIDI, knobs
        |                  |                    |
        v                  v                    v
  main thread:  Yjs provider · clock sync · data channels
                parse and apply the document
                composer scheduler (wasm, this thread, as native)
        |
        |  command queue: NOTE_ON/OFF, SET_CHAN_ARG, SET_CHANNEL,
        |  each stamped with the frame it applies at
        v
  AudioWorklet: libthink + plugins, one wasm module
                process() per window; ring to the 128-frame quantum
```

Decisions, and why:

**The scheduler stays on the main thread.** It is GUI-thread code today and
the worklet has to stay lean. It is driven from the audio clock rather than
the 20 ms timer: the main thread reads `AudioContext.currentTime`, ticks the
scheduler ahead by a lookahead, and posts commands stamped with the frame
they apply at. The worklet applies a command at its frame rather than at the
top of `process()`. That is the one change to the command path, and it is
what makes a window of 256 mean 256 rather than "somewhere in the next 256".
`g_get_monotonic_time` in the glibmm shim becomes the audio clock, not the
wall clock; the two drift and only one of them is the truth.

**Plugins link statically for the browser.** Under Node the wasm build loads
side modules through `dlopen`, which is what keeps `compare.mjs` honest
about the native loader. An `AudioWorkletGlobalScope` has no `fetch` and no
file system, so nothing there can `dlopen`. The browser bundle links every
plugin into the main module and registers them through a table keyed
`category::name` behind the same seam `thPlugin.cpp` already puts around
`dlopen`. Both bundles come from the plugin list in `plugins/CMakeLists.txt`,
so neither can drift. The side-module tree is 11 MB, mostly each module's
own copy of libc; static should land near the 1.8 MB main module.

**No SharedArrayBuffer.** Emscripten's own `-sAUDIO_WORKLET` wants wasm
workers, which want `SharedArrayBuffer`, which wants cross-origin isolation
headers that make embedding the thing anywhere a chore. The tree's design
needs none of it: fetch the wasm on the main thread, post its bytes to the
worklet through its port, compile and instantiate them there, and talk
through `postMessage`. (The bytes rather than a compiled
`WebAssembly.Module`: Firefox accepts a Module on an AudioWorklet's port,
Chrome delivers it as a `messageerror`.) Two threads, one queue, as now. If message
latency ever shows up as a problem the fix is a ring in a shared buffer, and
that is a later optimisation with a known cost, not a foundation.

**The editor is text.** CodeMirror with the Yjs binding gives shared editing
with everyone's cursors. The node canvas is a substantial gtkmm investment
and porting it is a separate project; a text-first tool is also the thing
none of the existing browser modulars are.

## 4. The network

**Relay from day one.** A small self-hosted server does three jobs: WebRTC
signalling, the Yjs document (y-websocket or Hocuspocus, so the document
persists and a late joiner has somewhere to fetch it from), and the
reference clock. Peer-to-peer without any server is possible for two people
and a trap for four.

**Gestures go peer-to-peer.** One data channel per pair, `ordered: false`,
`maxRetransmits: 0`, mesh. A mesh is fine to about six peers; past that the
relay fans out and the latency is what it is.

**One clock.** Each peer estimates its offset to the relay's clock with a
periodic ping, keeping the lowest-RTT samples, the way NTP does. The
transport's origin is a relay-clock time. Beat position is integrated from
the origin across tempo changes, which the scheduler already does for
beat-valued durations. A tempo change is a transport event stamped with the
beat it applies at; every peer applies it at that beat.

**Messages**, sketched:

```
transport   { origin, tempo, seed, playing, atBeat }
knob        { name, value, atBeat, from }            latest atBeat wins
note        { seat, note, velocity, atBeat, mode }   mode: direct | quantised | ahead
noteoff     { seat, note, atBeat }
ping / pong { sent, received }
```

Everything carries a beat, not a millisecond, so a message is meaningful on
a peer whose clock differs by whatever the estimate missed.

## 5. Seats and editing

**A seat is a MIDI channel.** Sixteen exist. Joining claims one; the piece's
`instrument` blocks say what is on it. Your input goes to your channel, or
into a chain whose `inputMidi` is set, which is how the quantised mode works.
Piece knobs are shared and latest-wins.

**Anyone edits the document.** That is what a CRDT is for. The subtle part
is not the merge, it is *when an edit takes effect*, because two peers
applying a structural change at different beats have different stage state
from then on and their tapes diverge.

The rule: a document change is applied at the next bar boundary, on every
peer, at the same beat. Stages are identified by chain name and stage name.
A stage whose text did not change keeps its state. A stage whose text
changed, or that is new, is re-created at the apply beat with the seed it
would have had from the file (seeds derive from the master seed and the
stage's position, so this is already defined). A stage that is gone is torn
down. A changed `instrument` block goes through the existing patch-swap
path, which cuts sounding notes on that channel, and that is documented
behaviour rather than a bug.

This is the piece of the design most likely to be wrong in a way that only
shows up with two people. It gets a harness before it gets a UI: two
schedulers in one process, a scripted sequence of edits at scripted beats,
tapes compared. `gencheck` is the template.

## 6. Milestones

Each one has a thing you can do at the end and a check that says it works.

**M0 — parity on record.** Land the determinism fix. Commit `wasm/`. Put
`compare.mjs` in CI under an emsdk. *Done when* CI runs the comparison and
the native gates still pass with the patch in.

**M1 — a synth in a tab.** Static-plugin bundle, worklet host, command
stamps, window 256, a text box holding one `.dsp`, the computer keyboard as
the keyboard. Report `baseLatency` and `outputLatency` in the page. *Done
when* a shipped patch plays from the keyboard in Chrome and Firefox on Linux,
macOS and Windows, and `dspab` at 256 against 1024 is clean or every
difference is understood.

**M2 — a piece in a tab.** The scheduler on the main thread against the
audio clock; the `.gen` loader; knobs as sliders. *Done when* every seeded
piece plays, and the tape the browser delivered matches the tape
`genwav.mjs` delivered under Node for the same seconds, in Chrome and in
Firefox. That is the wasm-against-wasm determinism gate, and it stays.

**M3 — two tabs.** The relay, the Yjs document bound to the editor, clock
sync, data channels, seats, knobs and direct-mode notes. *Done when* two
browsers on one machine share a piece, both can edit it, and their tapes are
identical from the same origin. Then the same across two machines on one
LAN, with the round trip shown.

**M4 — edits and arrivals.** The apply-at-bar rule from section 5, with its
harness first. Late join by fast-forward. Quantised and play-ahead modes.
*Done when* a peer joining at 90 seconds matches the room's tape from that
point, and an edit made on one peer produces the same tape on the other.

**M5 — a URL.** Rooms, links, the relay deployed beside the site, a patch
library to pick instruments from. *Done when* four people in two cities play
a piece for twenty minutes and nobody asks which mode they are in.

**M6 — later, if wanted.** Web MIDI input. Recording the tape and the mix.
The node canvas. Voice chat as an ordinary WebRTC audio track, which is
independent of everything above and can be dropped in at any point.

## 7. Risks, ranked

1. **Structural edits mid-piece** (section 5). Only shows with two peers,
   only reproducible with the harness. Build the harness before the UI.
2. **The window length.** A plugin that assumed 1024 changes sound at 256.
   `dspab` finds it; the fix is per plugin and small.
3. **Per-note allocation in the worklet.** A note copies a tree and its args
   size themselves on the first window ([ARCHITECTURE.md](ARCHITECTURE.md#it-is-not-hard-rt-safe-yet)).
   wasm `malloc` is cheap and the worklet is not hard real time, but sixteen
   voices arriving on one quantum is the thing to measure. The listed fix,
   sizing on the main thread before enqueue, is the same fix here.
4. **Output latency on Windows.** Shared-mode WASAPI through Chrome is
   20–40 ms before the network. Report it and let the seat pick a mode;
   there is nothing else to do about it from a page.
5. **Bundle size.** Static plugins should come in around 2 MB of wasm.
   If they do not, the plugin list is explicit and a browser subset is one
   CMake variable.
6. **Clock drift between the audio clock and the relay clock.** The
   estimate is re-taken continuously and the scheduler is driven from the
   audio clock only; the relay clock is used to agree on an origin and
   never to time a note.

## 8. Not doing

- Streaming audio between peers. The whole design exists so as not to.
- Peer-to-peer with no server. See section 4.
- Mobile. Web MIDI and worklet behaviour on iOS are their own project.
- The node canvas, until the text tool is real.
