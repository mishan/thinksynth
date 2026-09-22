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
- `scripts/dspab -B 256` over the corpus: every shipped DSP renders the
  same at 256 as at 1024. Three did not, and all three had a cycle in the
  graph -- the engine breaks one by letting a node read the previous
  window, so the loop's delay *is* the window: 23 ms at 1024, 5.3 ms at
  256. Two of them were rebuilt with the loop inside a plugin
  (`dsp/waveguide.dsp`, `dsp/sandh.dsp`) and the third kept as
  `scripts/guard/feedback.dsp`, which is where the claim is checked now.
  Understood, and no plugin read the window length as a constant.
- `wasm/web/check.mjs` plays every shipped patch through the module from
  Node. `wasm/web/browsertest.mjs` renders a phrase through the worklet in
  headless Chromium and Firefox and matches the module run directly,
  sample for sample; the page itself starts, loads, takes keys and reports
  latency in both. The CI `wasm` job runs both.
- What is left for done: playing it on real hardware, in Chrome and
  Firefox, on Linux, macOS and Windows. A headless browser has no sound
  card to hear.

### M2, so far

The step-size fix is its own branch and its own PR, `jam-step-fix`, since it
changes what the shipped pieces compose. What follows is on `jam-m2`, which
starts where that ends:

- The sixteen composers join the 62 DSP plugins in the static bundle, and
  the composer host -- `thcScheduler`, `thcPlugin`, `thcGenFile`,
  `thcNodeHost` -- is compiled into the module beside them. 268 KB of wasm
  becomes 602 KB. `thDynLib`'s static table stopped being a table of the
  DSP ABI's four function pointers and became a name and a list of symbols,
  because there are two ABIs behind that seam now and neither belongs in a
  file whose job is to stand in for `dlopen`.
- A composer's exports are `extern "C"`, which is a linkage and not a
  scope, so a namespace apiece is not enough as it was for the DSP
  plugins: the build renames each export before compiling the plugin
  inside its namespace, and the table looks it up under the new name.
- `composer_draw` is the one export that does not come. Nine composers
  draw with cairo, there is no cairo in a worklet and nothing to draw on,
  so they are compiled with `THC_NO_DRAW` and both the function and its
  `<cairo.h>` are left out. `thcPlugin::hasDraw` already answers for an
  absent one. The canvas that would call it is section 3a's, and M6's.
- The scheduler runs in the worklet, stepped once per window from inside
  `tw_render`: apply the commands due in this window, step the transport by
  the window's own length in seconds, render. That is `genwav`'s loop, to
  the line. No timer, no lookahead, and nothing a background tab throttles.
- Load, transport, knob and MIDI-in commands, each stamped with the frame
  it applies at, on the queue the notes already used. The page is the
  nearest peer and not a privileged one. The tape comes back the other way
  in batches -- every 16 quanta, or on a flush -- with the transport's
  position and an epoch a rewind bumps, which is how the page knows to
  clear its roll.
- A key in piece mode is a `THC_EV_NOTE` held with no duration and a
  `THC_EV_NOTEOFF` to let it go, on a channel the page picks, which is
  what a chain's `input midi` is matched against. So `hands.gen` -- three
  chains, no generators, no instruments, nothing but what you play -- is
  playable in a tab: hold a chord and the arpeggiator breaks it. It
  declares no instruments, so the page fills the channels it names from
  the same four patches the desktop's first run loads, and offers each of
  them as a row to aim by hand — the piece first and the aiming after, so
  what a channel sounds like is never what the page did before.
- `wasm/twevent.h` and `wasm/tape.mjs` are one spelling of an event,
  shared by the Node host and the browser's: M2's gate is that two tapes
  are the same tape, which is a claim about the piece and not about two
  ways of printing a number.
- The page has a piece mode: the `.gen` in a text box, the shipped pieces
  in a menu, Play/Stop/Rewind, the knobs the piece declared as sliders, and
  a piano roll of what came back.
- And keys on screen, in both modes, so the thing is playable on a device
  with no keyboard to borrow. An `<svg>` whose viewBox is in key units --
  a white key is 1 wide -- so the same widget is two octaves on a phone and
  four on a desktop, with the keys a finger wide either way rather than the
  same fraction of two different screens. Several fingers at once, a drag
  across the keys as a glissando, and one press/release path shared with
  the computer keyboard: a note held by both is one note, and a note is
  released by the route it was pressed by, even if the mode or the channel
  moved under it. The page itself folds its source boxes away on a narrow
  screen and pins the keys to the bottom of a short one, which is what a
  phone held sideways to play needs. It is a mode and not a second panel
  because a piece takes the channels it asks for and the first of those is
  channel 0, where the keyboard's patch was -- deliberately, since a
  `setChannelTaken` hook would move every instrument by one and the tape
  names channels. The keyboard still plays in piece mode: into the piece,
  through `input midi`, which is the path a peer's keyboard will take.
- A tape comparison cannot see a key: a seeded piece composes the same
  whoever is listening. So `piececheck.mjs` ends by holding a chord in
  `hands.gen` and asserting what comes back is a *figure* and not the
  chord -- the arp eats what it is handed, so a press passed straight
  through would look like success and mean the arp never saw it.
- **The gate passes.** `wasm/web/piececheck.mjs` composes every seeded
  piece in the browser module for a minute at 48 kHz and 44.1, in windows
  of 256 and of 128, and diffs each against the tape `genwav.mjs` delivers
  under Node -- a different module, plugins dlopened rather than linked in,
  the transport stepped by a fixed virtual clock in windows of 1024. All
  thirteen are identical at all four steps. `browsertest.mjs` runs the same
  comparison through the worklet in headless Chromium and Firefox at 256
  and at 128: identical there too. Both are in the CI `wasm` job.
- `wasm/web/bench.mjs` is the quantum measurement. On this desktop, with
  every seeded piece and a six-note chord pressed and released twice a
  second on top: the worst quantum of all runs 1.6 to 2.4 ms across
  repeats, against a 2.67 ms budget, and it is always the *first* one --
  the one that builds every instrument's graph, before the transport has
  moved and before there is audio for it to interrupt. Once the transport
  is moving the worst of all of them is about 0.6 ms, a fifth of the
  quantum, and p99 stays under 0.4 ms. That is the measurement that could
  have sent the scheduler back out of the worklet, and it did not.
- What is left for done: the same bench and the same pieces on a slow
  machine and on real hardware, in Chrome and Firefox. A fast desktop and a
  headless browser are not the case that decides it.

### M3, so far

On `jam-m3`, which starts where `jam-m2` ends. Where it stands:

- The scheduler seam: a stop, a
  tempo or a knob is stamped with the transport time it applies at and
  applied at that time inside the step, the transport stepped to it
  exactly, on every peer whatever its window or rate; a start is armed at
  an origin frame and begins with a partial first step so that transport
  zero is that frame; every window steps *to* its end time rather than
  *by* a window, so the clock never drifts from the frames; a late command
  is applied at once and counted. `genwav.mjs -c` applies the same
  commands the same way, so the reference tape can carry a command
  stream too.
- `wasm/web/clock.js` and `commands.js`: the relay-clock offset from the
  shortest of the last few pings, the audio clock as a line fitted through
  what the context reports, transport time as frames from the origin; and
  the commands, made and applied the same way by the sender and every
  receiver.
- **Gate 8.1 passes.** `wasm/web/protocoltest.mjs` runs two peers in one
  process -- the browser module at 256/48 kHz and at 1024/44.1 kHz, blocks
  out of phase -- over a simulated network of 40 ms with 20 ms of jitter
  and 2% loss, with Play from one side and knobs and tempo changes from
  both. Every seeded piece gives one tape on both peers, and it is the
  tape genwav delivers under the same commands. Over 300 ms, past the knob
  lead, the late knobs are counted and named. In the CI `wasm` job.
- Found on the way, and fixed in the scheduler: a rewind did not compose
  what a load composed. `orrery`'s lead differed from the first bar,
  because a load creates a composer over the plugin's defaults and then
  announces the file's values, while a rewind re-created it over the final
  values and announced only the bindings, and `gen::evolve` draws
  randomness on both. A rewind now redoes what the load did, and
  `gencheck` gates every seeded piece's rewind against its load. Every
  peer's Play is a rewind, so this would have parted the peers before the
  network had a chance to.
- The relay (`wasm/web/relay.mjs`): the document over y-websocket's
  protocol, the clock, presence and seats, signalling, and the relayed
  path for gestures the mesh cannot carry. `relaytest.mjs` drives it from
  Node and is in CI.
- The page (`jam.html`, `jam.js`): a room joined by name, the piece's
  text shared through the relay and edited in CodeMirror with everyone's
  cursors, the peers connected over WebRTC data channels with the relay as
  the fallback, seats as the piece's instruments, Play as a start from an
  agreed origin, knobs, tempo and keys as commands, and the numbers --
  the clocks' round trip and spread, the late count -- on the page. It is
  bundled by esbuild from the CMake build, so `npm ci` in `wasm/web` comes
  once before the configure.
- **Gate 8.2 passes.** `wasm/web/jamtest.mjs` starts a relay and a site,
  puts a Chromium page and a Firefox page in one room, each with a live
  `AudioContext`, presses Play on one, moves a knob from each side and the
  tempo from one over thirty seconds, and holds the two tapes against
  each other and against genwav's under the same commands: one tape,
  nothing late, in three runs of three. In the CI `wasm` job. The two
  transports come out about 40 ms apart by the relay's clock, which is
  the two browsers' output timestamps disagreeing about where their
  output is; the tape does not depend on it.
- Found on the way: a least-squares line through the audio clock's
  samples put a Chromium peer's origin 400 ms from a Firefox peer's,
  because a sample reported before the output stream had started ticking
  tilted the line. The clock now estimates the offset alone, by a median,
  at a slope of one. And a page's reading of its own transport can be
  stale by fifty milliseconds in headless Firefox, which makes a stamp
  earlier than it means to be and eats the lead; the page takes the
  fresher of two readings.
- Not yet: gate 8.3, two machines on a LAN, by hand.

### M6, so far

On `jam-m6`. Where it stands:

- The engine change the mirror needs: a `thSynth` that never renders
  (`setSilent`), dropping notes at the door and applying everything
  else, gated in `gencheck` against a rendering synth over every seeded
  piece.
- The desktop's two canvases split into content and shell: what they
  draw and what a click means is C++ with no toolkit in it, compiled
  without gtkmm on the include path; the gtk widget around each is a
  few dozen lines. The same content classes are what the browser will
  run.
- The cairo stand-in they will run through: `cairo-canvas2d`, cairo's own
  API over a display list the page replays on a Canvas2D, a package of its
  own with its own build and test and nothing of this tree on its
  include path. The browser build draws through it already -- every
  composer's picture, gated over the whole corpus at two sizes.
- The mirror: the same module again with a synth that never renders, fed
  the messages the worklet is fed through one shared handler and stepped
  to the frame the worklet's tape batch reached. Every seeded piece
  composes one tape over the two.
- The composer canvas compiled into the module and drawing the piece,
  and a click on a composer's picture as one more stamped command --
  made where the rectangle was drawn, applied at its time on every peer.
- The mirror in a worker, the piece's picture on both pages, and the two
  tapes held against each other and counted -- a determinism check a room
  gets for nothing. Two browsers in a room paint the same Life board on
  the same beat.
- The node editor over the shared document: the desktop's graph, canvas
  and writer compiled to wasm, the palette and the params panel as HTML,
  and every edit a splice into the room's `.dsp` -- held, byte for byte,
  against what the desktop's own writer produces.
- Probes: a tap in the worklet on what is being rendered, the samples to
  the page with the tape, a visual module drawing them in the page's own
  instance, and a panel on the node the canvas draws with the rest of the
  graph.
- The node editor on the solo page as well, over the patch in its text
  box rather than over a document -- one file source, two pages.
- A channel's parameters, described once by the module and drawn by the
  page (`src/PanelModel.h`, `wasm/web/panel.js`): the panel the desktop has
  had for twenty years and this page never had. An edit of a row is a
  command like a knob move -- the page that typed it applies it by
  receiving it back, the same as every other peer. The description is held
  against the desktop's, byte for byte, by `wasm/web/panelcheck.mjs`.
- A `.patch`, read by the module rather than by the page
  (`src/PatchFile.h`, `src/PatchApply.h`). The page fetches the file and
  hands the text over; the graph it names, its side, its effect and its
  overrides all land in the order the format requires. The page's own parser
  is gone, and with it the two things it got wrong -- a channel effect
  dropped on the floor, and `side` sent to the engine as a chanarg called
  `side`. `wasm/web/patchcheck.mjs` holds the module's reading against the
  desktop's, byte for byte. The slots are shared too (`src/PatchSet.h`), so
  the channel row can say which file is on a channel and whether it has been
  edited since -- which the desktop has lit a Save button off since 2004 and
  the page could not say at all. And a Save beside it: the module composes
  the bytes the application writes and the page hands them over as a
  download, so a patch tweaked in a browser is a file that opens on the
  desktop rather than one that lasted until the tab closed.
- And the knobs a piece declares, off the same description
  (`src/KnobPanel.cpp`) and drawn by the same renderer on both pages. A
  knob is where the two deliveries differ and can be seen to: the panel
  describes it and a stamped `knob` command moves it, so it lands at the
  same transport time on every peer.
- Both pages tiled rather than stacked (`wasm/web/panes.js`): the panels a
  person wants side by side -- the piece, the keys, the parameters, the
  graph -- in splits with a divider each, tabs, a drawer and a chord for
  every command. It adopts the markup and creates nothing, so a narrow
  screen or a finger gets the document it always was, and it is what lets
  a pane nobody is looking at stop drawing: two wasm canvases stacked as
  tabs cost one picture a frame, not two.
- Not yet: the by-hand pass in two browsers (M6's gate 8.4).

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
- **The audio thread sees no network.** The network lives entirely on the
  main thread, in front of the command queue, where the GUI lives now. What
  crosses the queue is commands stamped with the frame or beat they apply
  at, whether they came from the keyboard, the page or a peer. The worklet
  cannot tell which, and that is the point.

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
of the next `process()`. The web build runs at 256. The ring between
`process()` and the device already handles a device period that is not the
window, so this was a number, not a rework; M1 ran `scripts/dspab` at 256
against 1024 and the only DSPs that moved are the three with a cycle in
their graph, whose feedback delay *is* the window (section 0).

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
  CodeMirror + Yjs    play/tempo/seed      keys, Web MIDI, knobs, clicks
        |                  |                    |
        v                  v                    v
  main thread:  Yjs provider · clock sync · data channels
                parse the document; pick the beat an edit applies at
        |
        |  command queue: NOTE_ON/OFF, SET_CHAN_ARG, SET_CHANNEL,
        |  LOAD_PIECE, TRANSPORT, KNOB, COMPOSER_INPUT --
        |  each stamped with the frame or beat it applies at
        v
  AudioWorklet: libthink, the plugins and the composer scheduler,
                one wasm module. process() per window, the scheduler
                stepped once per window; ring to the 128-frame quantum
        |
        |  the tape (delivered events), transport position, status
        v
  main thread:  piano roll, knobs, transport; later the mirror (3a)
```

Decisions, and why:

**The scheduler runs in the worklet.** This reverses the first draft of this
plan, which had it on the main thread stepped ahead of the audio clock.
Both were measured and the reasons are these. The scheduler holds a
`thSynth *` and calls it directly,
and much of what it does never appears on the tape: the note-offs it derives
from durations, the chanarg writes a knob binding makes, instrument
application at load and rewind, the flushes at stop. A main-thread
scheduler would need a bridge forwarding all of that as stamped messages,
plus a second synth on the main thread for it to hold, which has to be
processed or its command queue fills. A bridge bug leaves the tape right
and the audio wrong, which is exactly what the tape gate cannot see. In the
worklet the scheduler drives the real synth the way `genwav` does, stepped
once per window by the audio clock itself: no lookahead, no timer, nothing
a background tab can throttle. The cost was measured at 0.05 ms per step at
p99 against a 2.67 ms quantum, with the one expensive step, the first,
landing before audio starts.

**The tape must not depend on the step.** Found on the way: `runDueTicks`
handed every composer it woke the *end of the step* as `now`, so each wake
was late by up to a step, the next was scheduled from the late one, and the
lateness compounded. The tape was a function of the step size. Two peers at
44.1 and 48 kHz would have composed different pieces from one file and one
seed, and on the desktop two live plays of a seeded piece never matched
either; only `gencheck` and `genwav`, with their fixed virtual clock,
repeated. The fix ticks each composer at its own wake time, with the
chain's control-rate nodes stepped to that time first; with it, all 13
seeded pieces give the same tape at 1024 and 256, at 44.1 and 48 kHz, and
under jittered steps of 2 to 60 ms. It changes what the existing pieces
compose, so it is its own PR with `gencheck`, ctest, `compare.mjs` and a
listen. One thing to add to it: a wake returned at or before now is now
re-armed from the wake time rather than the step end, so a composer that
keeps returning its own wake time used to run once per step and can now run
up to a thousand times per second of step. Cap it.

**Every input is a stamped command, the page's own included.** A knob
moved, a key pressed or a Life cell clicked has to be applied at the same
beat on every peer or their tapes diverge from that beat on. So the local
page has no privileged access to the scheduler. It is the nearest peer, and
its commands take the path a remote peer's do. The composer ABI's promise
that tick, receive, param access and input never run concurrently still
holds: commands are applied at the top of a step, from the queue, on the
one thread that ticks. The one export that cannot run there is draw, which
is what section 3a is about.

**Plugins link statically for the browser.** Under Node the wasm build loads
side modules through `dlopen`, which is what keeps `compare.mjs` honest
about the native loader. An `AudioWorkletGlobalScope` has no `fetch` and no
file system, so nothing there can `dlopen`. The browser bundle links every
plugin into the main module and registers them through a table behind the
same seam `thDynLib` already puts around `dlopen`; M1 did this for the 62
DSP plugins and they came to 268 KB of wasm. The composers join them in M2.
Both bundles come from the plugin list in `plugins/CMakeLists.txt`, so
neither can drift.

**No SharedArrayBuffer.** Emscripten's own `-sAUDIO_WORKLET` wants wasm
workers, which want `SharedArrayBuffer`, which wants cross-origin isolation
headers that make embedding the thing anywhere a chore. The tree's design
needs none of it: fetch the wasm on the main thread, post its bytes to the
worklet through its port, compile and instantiate them there, and talk
through `postMessage`. (The bytes rather than a compiled
`WebAssembly.Module`: Firefox accepts a Module on an AudioWorklet's port,
Chrome delivers it as a `messageerror`.) Two threads, one queue, as now. If message
latency ever shows up as a problem the fix is a ring in a shared buffer, and
that is a later optimisation with a known cost, not a foundation. Headless
Firefox was seen to hand the worklet its messages in batches about every
10 ms; in this design that bounds how soon a knob is heard and how fresh
the page's tape is, never when a composed note sounds.

**The document is text.** CodeMirror with the Yjs binding gives shared
editing with everyone's cursors, and a text-first tool is the thing none of
the existing browser modulars are. The node canvas and the composer view
are views of that text, and both are wanted soon; section 3a is how they
fit.

### 3a. The composer view and the node editor

Neither changes the placement above, for different reasons.

**The node editor is independent of the scheduler.** It edits the `.dsp`
text in the document and reads probes, and probes come from the worklet in
every design. It is [NODE_EDITOR.md](NODE_EDITOR.md)'s model over a
canvas, with the document underneath instead of a file. Nothing it does
goes near the scheduler.

**The composer view is a mirror.** The desktop window's forty-odd calls
into the scheduler reduce to a few commands (start, stop, reset, tempo,
mute, bind, inject), a status snapshot (now, running, what is pending) and
the delivered-event stream; chains, instruments and sinks it reads from the
document. All of that crosses a port without complaint. What does not is
`composer_draw`: nine composers paint their state with cairo, on the tick
thread, straight from instance memory. There is no cairo in a wasm main
thread either, so draw ports as-is *nowhere* in the browser, whichever
thread the scheduler is on.

Determinism gives the answer. A second scheduler fed the same stamped
commands holds the same state, because that is what the jam already relies
on between peers. So the composer view runs a mirror: another scheduler on
the main thread or in a worker, another peer that renders nothing, holding
real composer instances. Draw can then run against those instances
unchanged, with cairo compiled to wasm and blitted to a canvas, or through
a snapshot export drawn in JavaScript. That choice waits until the view is
started; it is the same choice under any placement and the only real cost
the view adds. Two things the mirror needs from the engine: a drain mode
for its synth, applying commands without rendering DSP, since a
non-rendering synth was measured filling its 1024-deep command queue and
dropping commands within one fast-forward; and the same command stream the
worklet gets, which the network layer already produces. Diffing the
mirror's tape against the worklet's is a free, continuous determinism
check. A mirror bug is a wrong picture and never a wrong sound.

Why not the other way round, with the scheduler on the main thread and a
bridge to the worklet: a bridge forwards *effects*, most of them off the
tape, while the mirror forwards *inputs*, all of them already stamped for
the network. Forwarding inputs is strictly less. And the mirror is most of
that alternative anyway. If real hardware ever shows the worklet has no
headroom, promoting the mirror to authoritative and adding the bridge is
the fallback, and nothing built for the mirror is lost.

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
transport   { at, op: start | stop | tempo, origin, seed, bpm }
knob        { at, name, value, from }                latest at wins
note        { at, seat, note, velocity, mode }       mode: direct | quantised | ahead
noteoff     { at, seat, note }
ping / pong { sent, received }
```

Everything carries `at`, a transport time, not a wall-clock millisecond,
so a message is meaningful on a peer whose clock differs by whatever the
estimate missed. The unit is transport seconds, the scheduler's own clock,
with beats derived from it.

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

In the worklet that costs: stage teardown and creation at the apply beat,
under a millisecond measured, so one quantum at most; and for a changed
instrument, parsing the `.dsp` on the audio thread, which M1's patch reload
already does and which is a dropout on every peer at the bar. That dropout
is the same under any placement, since the worklet has to parse the text
itself either way. Parsing in the windows before the bar and swapping at it
is the mitigation, and it is later work.

Late join is the same machinery run long: the worklet fast-forwards the
scheduler from zero to now before audio resumes, with delivery suppressed.
Nothing reaches `addNote` until the transport catches up, or thousands of
note graphs get built for notes that will never sound.

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

**M2 — a piece in a tab.** First the step-size fix from section 3, as its
own PR: `gencheck`, ctest, `compare.mjs` and a listen to what changed. Then
the composers in the static bundle, the scheduler in the worklet stepped
once per window, the load, transport, knob and input commands, the tape
posted back, and knobs as sliders with a piano roll on the page. *Done
when* every seeded piece plays, and the tape the browser delivered matches
the tape `genwav.mjs` delivered under Node for the same seconds, in Chrome
and in Firefox, at 256 and at 128. That is the wasm-against-wasm gate and
the step-invariance gate in one, and it stays. Also done here, on real
hardware: the worst quantum on a slow machine with the busiest pieces
(`orrery`, `tide`) plus a chord of note-ons, which is the one measurement
that could send the scheduler back out of the worklet.

The gates pass and the bench is written; what is left is a slow machine and
a sound card. `COMPOSER_INPUT` is the one command on section 3's list that
is not here, and it is blocked rather than skipped: a gesture on a
composer's picture is in the coordinates that picture was drawn in, and
nothing in a worklet draws. It arrives with the mirror, in M6.

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

**M6 — the composer view and the node editor.** Wanted soon, and
depending on M2 only, so this can run alongside M3 to M5. The composer
view is the mirror from section 3a: the synth's drain mode, a second
scheduler fed the command stream, and the cairo-in-wasm or snapshot choice
made then. Composer input arrives as stamped commands. The node editor is
[NODE_EDITOR.md](NODE_EDITOR.md)'s model over the document, with probes
posted from the worklet. *Done when* a Life board in a shipped piece can be
clicked on one peer and the other peer's tape follows, and a `.dsp` edited
on the canvas plays the same on both.

**M7 — later, if wanted.** Web MIDI input. Recording the tape and the mix.
Voice chat as an ordinary WebRTC audio track, which is independent of
everything above and can be dropped in at any point.

## 7. Risks, ranked

1. **Structural edits mid-piece** (section 5). Only shows with two peers,
   only reproducible with the harness. Build the harness before the UI.
2. **A composer whose tape depends on the step.** One whole class was
   found and fixed (section 3); the cap on re-arm iterations is part of
   that fix. Every new composer goes through the step-invariance gate in
   M2 before it ships, because this failure is silent between peers.
3. **Worklet headroom on a slow machine.** Composer bursts plus a chord of
   note builds in one quantum. Small headless; measured on hardware in M2.
   If it overruns, the mirror becomes authoritative and gets a bridge.
4. **Per-note allocation in the worklet.** A note copies a tree and its args
   size themselves on the first window ([ARCHITECTURE.md](ARCHITECTURE.md#it-is-not-hard-rt-safe-yet)).
   wasm `malloc` is cheap and the worklet is not hard real time, but sixteen
   voices arriving on one quantum is the thing to measure. The listed fix,
   sizing before enqueue, is the same fix here, though in this design the
   enqueue is the scheduler's and also on the audio thread.
5. **A dropout on every peer at the bar** when an instrument changes
   (section 5). Same under any placement. Pre-parse before the bar when it
   starts to matter.
6. **Output latency on Windows.** Shared-mode WASAPI through Chrome is
   20–40 ms before the network. Report it and let the seat pick a mode;
   there is nothing else to do about it from a page.
7. **Firefox's worklet message batching.** About 10 ms headless, unmeasured
   on hardware. Bounds knob-to-ear and tape freshness only.
8. **Clock drift between the audio clock and the relay clock.** The
   estimate is re-taken continuously and the scheduler is driven from the
   audio clock only; the relay clock is used to agree on an origin and
   never to time a note.

## 8. Not doing

- Streaming audio between peers. The whole design exists so as not to.
- Peer-to-peer with no server. See section 4.
- Mobile. Web MIDI and worklet behaviour on iOS are their own project.
- A visual-first tool. Text stays the document; the canvas and the
  composer view are views of it (section 3a).
