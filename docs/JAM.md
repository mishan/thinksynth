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
  the libm straggler above. It runs on every push, so M0 is done.

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
  graph -- the engine then broke one by letting a node read the previous
  window, so the loop's delay *was* the window: 23 ms at 1024, 5.3 ms at
  256. Two of them were rebuilt with the loop inside a plugin
  (`dsp/waveguide.dsp`, `dsp/sandh.dsp`) and the third kept as
  `scripts/guard/feedback.dsp`. A graph with a loop now runs a sample at a
  time and renders the same at both.
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
  `--json FILE` writes the same runs, with the node version, CPU model,
  the script's commit and the build's path and module hash, as one
  document for comparing machines. It plays every piece the way the page
  does -- the kit's samples loaded, the channels a piece leaves to the
  page given their default patches, an unseeded piece a fixed seed --
  and `--only a.gen,b.gen` and `--seconds N` cut a run down.
- `wasm/web/worklettime.mjs` is the same question asked of a browser: the
  solo page plays each piece in a real-time AudioContext, under
  `scripts/headless.sh`, and the worklet's own counts of its `process()`
  calls are read back through `window.solo.quanta`. It prints the calls
  over the quantum's budget per minute, the slowest call and the p99, and
  the audio clock against the wall clock, and `--json FILE` writes them:

  ```
  node wasm/web/worklettime.mjs --browser firefox --seconds 30 \
      --json ff.json build-web airports.gen fern.gen
  ```

  Not a gate: the counts belong to the machine and the browser, and the
  worklet's clock is a whole millisecond.
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
- Gate 8.3 passes, by hand: two machines on a LAN, one wired and one on
  Wi-Fi, the relay on the wired one, playing a piece together at a
  latency of 3 to 15 ms.

### M4, so far

M4 is done. Where it stands:

- **Late join by fast-forward.** A page that presses Start in a room
  already playing asks the relay for the run: the start, the document as
  that start named it, and every stamped command since. It loads that
  revision, puts transport zero at the start's origin -- a frame its own
  output went past before the page existed, so a frame below zero is now
  a frame and not "never started" -- and hands the worklet the begin and
  the commands in one message. The worklet steps the transport from zero
  to the present window by window, each command applied at its stamp,
  with the synth silent: the tape is the room's from the top, and nothing
  of the past reaches `addNote`.
- The stepping is spread over windows, two milliseconds of each, and the
  windows it spans are silent. Stepping ninety seconds of `tide` on a
  silent synth is under ten milliseconds of work, so it is caught up in
  a few windows; a single long process() would stall the context, and a
  stalled context falls behind the relay's clock for good. The mirror
  takes it in one step.
- The relay keeps the run. A start begins one, a stop ends it, and every
  page sends a copy of each stamped command beside its mesh broadcast.
  The document is snapshotted at the revision the start names, waiting
  for the starter's edit to arrive on the other socket if it has to: the
  document moves on during a run -- a param edit is spliced into it by the
  peer that made it -- and a joiner has to load what the others loaded.
  Keys in direct mode are not replayed; they were played where they
  arrived, and no two peers heard them at the same point in the piece.
- **The gate passes.** `protocoltest.mjs` adds a third peer, at 44.1 kHz
  and a window of 256, joining ninety seconds into a two-minute run of
  the four busiest seeded pieces, with knobs and a tempo change on either
  side of its arrival and knobs of its own once it has caught up. Its
  whole tape is the other two's and genwav's, nothing late, caught up
  about 100 ms after its Start. `jamtest.mjs` puts a third page into the
  room eighteen seconds in; it catches up once its clocks have their
  samples and its tape is the room's.
- **The edit harness.** An edit is a new text applied at one transport
  time. It is read into a staged scheduler first, so a text that does not
  load changes nothing, and the live one adopts it (`thcScheduler::adopt`,
  `src/thcGenDiff.h` for the rule): a stage whose chain, name and text are
  the same in both keeps its instance, its next wake and its bindings;
  everything else is built from the new text, with the seed its place
  there gives it. The knobs are the live ones, by name, so a moved knob
  stays where it was moved unless its declaration changed. An instrument
  whose declaration or files changed is loaded again; one that did not
  keeps sounding. Queued notes and the offs of sounding ones are delivered
  as composed.
- `scripts/editcheck` holds it: every seeded piece, two peers -- windows
  of 1024 against jittered steps of 2 to 60 ms -- given a comment, a
  changed param, a chain added and the chain taken away, at times on no
  grid. One tape through every edit; the channels no edited chain plays
  on deliver exactly the unedited run's tape, which is what "kept its
  state" means; the added chain is heard only between its edits; and a
  rewind afterwards plays what a fresh load of the final text plays.
- Found on the way, both in the scheduler and both invisible until two
  chains ticked out of phase. `beat_` was added up a step at a time, so
  its last bits depended on the steps, and a stage an edit created
  inherited them (`mirrorball`); it is now read off the last tempo
  change. And every delivery waited for the end of the step, so a note
  `xform::humanize` moved to before the tick that made it landed on the
  tape before or after another chain's notes depending on where the step
  ended (`loosen`); what is due is now delivered before each tick. Over
  two minutes of every seeded piece, 24 tapes are unchanged and 9 have
  lines in a different order; no event is added, lost or moved.
- **Apply is an edit.** While the transport runs, Apply sends the
  document's `.gen`, and every `.dsp` it has changed since the worklet
  last loaded one, as an `edit` command stamped for the first bar line
  past the transport lead. The text rides in the command rather than being
  read off the document by each peer: the document goes on moving, and
  every peer has to apply the one revision the sender pressed Apply on.
  The worklet and the mirror apply it through the same `TW_EDIT`, the
  relay logs it, and a late joiner steps through it. It goes by the room
  socket and not the mesh: a peer that missed one would play another piece
  from there. One made in a run a newer Play has replaced is dropped on
  arrival. Stopped, Apply is still Play from the top.
- A knob command names its knob now, and the name is looked up when the
  command applies. An index was a place in a list an edit can reorder.
- `genwav.mjs -c "AT edit FILE"` is the reference. `protocoltest.mjs`
  sends an Apply from one peer ten seconds into every seeded piece, and in
  the late-join run one edit before the joiner arrives and one after;
  `jamtest.mjs` changes a chain from one page mid-run. Every peer applies
  every edit, one tape, genwav's, and not the tape of the run nobody
  edited.
- A command that names a chain, a stage, a section or a knob by index --
  `mute`, `solo`, `section`, `knobwrite`, `param`, `input` -- carries the
  number of edits its maker had seen, and is dropped where another edit
  has applied since: an edit that adds a stage above it moves the index.
- **The three ways to play**: a mode beside the seat, with the relay round
  trip and what the mode costs at the tempo playing. Direct is unchanged.
  A quantised key is stamped for the first sixteenth at least the knob
  lead away, its release at least a sixteenth after that; a key a bar
  ahead is stamped exactly one bar on. Both are applied at their stamp on
  every peer, the player included, through a key command the worklet
  applies inside the step (`TW_NOTE`) -- so a key into a piece's
  `input midi` composes the same thing everywhere, and is logged for a
  late joiner. A key a bar ahead onto a plain channel is heard by its
  player at once and by everyone else a bar later.
- Keys stamped for one time are applied in an order made from the
  sender's id and counter, not in the order they arrived: two quantised
  seats meet on grid lines all the time, and a quantizer passes on what
  it is handed in the order it is handed it. `protocoltest.mjs` holds two
  peers' chords on one channel of `hands.gen`, eleven grid lines where
  they meet, to one tape and genwav's -- and fails with the tie taken
  out. `jamtest.mjs` plays `hands.gen` from two pages, one quantised and
  one a bar ahead.
- A seat is any channel the piece plays -- its instruments, the channels
  it takes `input midi` on, and the ones its sinks name -- where it was
  the instruments alone, which gave a piece like `hands.gen` no seats.
- A gesture on a composer's picture and a stage's param name the stage
  by chain and stage name as well as by index, and the worklet finds it
  by name when the command applies: an edit that adds a chain or a stage
  above it moves every index after it. A command whose stage an edit
  removed or renamed is dropped, and its sender stops waiting for the
  edit it would have written. One whose stage is still there is not
  dropped for an edit its maker had not seen, as one by index is. Stage
  names are unique within a chain.
  `protocoltest.mjs` sends a param and a gesture numbered for the piece
  before such an edit; by name each reaches its stage, and the same
  command without its names reaches the neighbor.
- A Play's seek is a field of its own. It had been written over the
  sender's id, so every Play came from one sender, and a second peer's
  first Play was dropped everywhere as a duplicate of the first.
- Keys played by hand are on the piano roll -- Direct keys and stamped
  ones, at the time they sound, on every peer's roll alike -- and a
  room's keyboard lights the keys every other seat is holding.
- By hand: two people in two homes about 40 miles apart played a room
  for over an hour over the deployed relay. The round trip and the count
  of late commands looked good. Latency is hard to judge by ear there,
  because the room gives little back about what the others are doing.

### M5, so far

Where it stands:

- **A deployed relay.** `docker/` builds the relay as an image, which CI
  publishes to GHCR from master, and the Pages site's `config.json` names
  the relay the `JAM_RELAY` repository variable does. Running one is
  [RELAY.md](RELAY.md).
- **Free play.** `gen/free.gen` is eight seats, each its own instrument,
  and nothing composed. Beside the seat, a picker lists the same graphs
  as the solo page's patch menu; a choice is a stamped `pick` command,
  applied at the next bar while playing and at once while stopped,
  through the edit path, and logged for a late joiner. A pick that would
  leave a chain riding a chanarg the new graph lacks is refused before
  it is sent.
- **The Sequencer in rooms.** The solo page's Sequencer as a room pane,
  its clicks stamped `input` commands by chain and stage name;
  `gen/seq.gen` is a step-sequencer piece to use it with.
- **Samples in rooms.** A Start hands the kit's wavs to the worklet and
  the mirror, as the solo page does, so an `osc::sample` instrument
  sounds in a room.
- **Text chat**, a pane on the room page (section 4).
- **Accounts.** A handle nobody else can join as, logged in with a
  passkey or an eight-word key from the relay; guests are marked as
  guests. The
  document socket is let in by a ticket from the room socket. Running
  it is [RELAY.md](RELAY.md#accounts).
- **An invite link**, so a room is joined without typing its name, and
  **a list of the relay's rooms** before joining.
- In progress: a rework of the room's layout.
- Not yet: TURN -- peers whose NATs defeat STUN fall back to the relay
  forwarding their commands; the `.patch` presets in the picker, which
  offers `.dsp` graphs; and the done-when, four people in two cities for
  twenty minutes.

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
  and writer compiled to wasm, the palette as HTML and the params panel
  off the same description the desktop draws (`src/NodePanel.cpp`),
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
- Both pages tiled rather than stacked (mullion, from npm): the panels a
  person wants side by side -- the piece, the keys, the parameters, the
  graph -- in splits with a divider each, tabs, a drawer and a chord for
  every command. It adopts the markup and creates nothing, so a narrow
  screen or a finger gets the document it always was, and it is what lets
  a pane nobody is looking at stop drawing: two wasm canvases stacked as
  tabs cost one picture a frame, not two.
- A composer stage's parameters, settable, off the same description again
  (`src/StagePanel.cpp`). The popover beside a stage box was a list of
  numbers to read; it takes them now, and a `param` command carries what
  was typed to every peer at the transport time it applies at -- for the
  same reason a knob does, since a `period` that changes a window earlier
  on one peer moves that stage's next firing. Each peer splices its own
  copy of the `.gen` off that one command, so no text crosses and the
  copies cannot part. Two browsers in a room are held to one tape across
  one of these by `wasm/web/jamtest.mjs`.
- The solo page's Sequencer in a room: a track per `gen::grid`, and a
  click on one the same stamped `input` command as a click on the
  composers' picture, names and all, written into the document by the
  peer who made it. `gen/seq.gen` is a piece for it; `jamtest.mjs` clicks
  a cell from one page and holds both tapes and both documents to it.
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
| delivery | CRDT (Yjs) over a relay | reliable, via the same relay | WebRTC data channels: unordered with no retransmit, and all but knobs reliable as well |
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
of the next `process()`. The web build runs at 128, one worklet quantum,
which is also the render's deadline: a longer window does all of its work
in one quantum and none in the next. The ring between
`process()` and the device already handles a device period that is not the
window, so this was a number, not a rework; M1 ran `scripts/dspab` at 256
against 1024 and the only DSPs that moved were the three with a cycle in
their graph, whose feedback delay was the window (section 0).

Then three ways to play, chosen per seat, with the measured round trip shown
next to the choice so nobody has to guess:

- **Direct.** Notes are scheduled on arrival. Right for peers in one city.
- **Quantised.** The key is stamped for the next grid line at least the
  knob lead away and applied there on every peer. A note lands on the
  grid, on every peer, and jitter shorter than the lead vanishes. With a
  16th at 120 bpm the player hears it up to the lead plus 125 ms late.
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

**Gestures go peer-to-peer.** Two data channels per pair, mesh: one
`ordered: false`, `maxRetransmits: 0`, carrying everything, and one ordered
and reliable carrying everything but knobs again, so that a key or a mute is
never lost and the first copy to arrive is the one applied. A mesh is fine
to about six peers; past that the relay fans out and the latency is what it
is.

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
edit        { at, text, files }                     the piece, at a bar
param       { at, chain, stage, row, text }         a stage's line, spliced
note        { at, seat, note, velocity, mode }       mode: direct | quantised | ahead
noteoff     { at, seat, note }
ping / pong { sent, received }
```

Everything carries `at`, a transport time, not a wall-clock millisecond,
so a message is meaningful on a peer whose clock differs by whatever the
estimate missed. The unit is transport seconds, the scheduler's own clock,
with beats derived from it.

**Speed is not in that list, and a room would have to put it there.** The
solo page has a speed control as well as a tempo — the tempo scales
beat-valued durations and reaches nothing in a piece written in seconds,
and the speed turns the transport clock itself, so everything moves. It is
a stamped command inside one page, applied on both of that page's
instances at the time it names, and it carries no further: two peers set
to different speeds would be playing the same piece at two rates and
agreeing on every `at` while they did it. Sharing it is a `transport`
op alongside `tempo` and the same `at` discipline; nothing else about it
is new, and the room page does not offer the control until it is there.

The page's own clock is already ready for it. A tape message carries where
transport zero falls as a frame *and* the speed, because the two together
are the line `TransportClock` walks to turn a transport time into a frame
(`clock.js`) — a clock that read the origin and assumed 1x would stamp
every command off by whatever the speed was. In a room it is 1, and read
rather than assumed.

**Chat is not a command.** A `chat` message goes over the relay's room
socket, reliable and ordered, and never over the mesh: it reaches no
worklet, no tape and no run a late joiner is handed. The relay stamps the
sender's id and name, sends the line to everyone in the room and back to
its sender, whose copy is how the page knows it went, and keeps none of
it, so a late joiner sees only what is said after it arrives. It refuses
a line with nothing in it but spaces and format characters, one over 500
characters and a peer past five lines a second, with a reason the page
shows under the box. A line carries the
bar.beat the sender's transport was at, when it was running. Between the
lines the page writes its own: who came and went, who took or left a
seat, Play, Stop, a seek, a tempo, an Apply, and a late joiner's
catching up -- what this page saw, from messages it
already gets. `channel` is `stage` for now; the house is the other one
(`JAM_BACKLOG.md`, 3.2).

## 5. Seats and editing

**A seat is a MIDI channel.** Sixteen exist. Joining claims one; the piece's
`instrument` blocks say what is on it. Your input goes to your channel, or
into a chain whose `inputMidi` is set, which is how the quantised mode works.
Piece knobs are shared and latest-wins.

**A seat's instrument is picked beside it.** The picker lists the shipped
graphs as the solo page's patch menu does, and a pick puts the instrument
block on the holder's seat on the new graph, dropping every value but `send`
(`thcGenEdit::setInstrumentGraph`): a value tuned for one graph, its level
included, is a refused load or a different sound on another. A pick whose
graph lacks a chanarg some sink rides is refused on the page that made it,
with the reason. Otherwise the picker splices it into the document, for the
next Play, and sends a `pick` command: every peer rewrites the piece as it
stands when the command applies and takes the result as an edit
(`thinkweb.cpp`, `applyPick`), so two picks on one bar both survive, and
nothing else in the document comes with it. At the next bar while playing; at
once while stopped. The seat list shows everyone what each seat plays. A
channel the piece leaves to the page has no block to rewrite and its picker
is shown disabled; `gen/free.gen` is the piece for a room that wants only
seats, an instrument each and nothing composed.

**A MIDI keyboard plays the seat.** *MIDI in* (`wasm/web/midi.js`) listens on
every Web MIDI input, note on and note off only, and hands each key to the
same press and release as the on-screen and computer keys, with its
velocity. The device's channel is ignored: the seat is the channel, on the
room page and the solo page alike. A focus change or a blur lets go of the
computer keys, whose keyups can be lost; it leaves MIDI keys down, since their
note offs arrive regardless. An input unplugged lets go of what it held.
Controllers are not read yet: a CC mapped to a knob has to be a stamped knob
command to land at the same time on every peer. The sustain pedal (CC 64) is
read on the solo page only, as a `SusPedal` chanarg, for the same reason.

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

**The piece is switched from the room.** The Piece menu beside the transport
lists the shipped pieces. A switch asks the relay, which rewrites the document
in one transaction: every file removed, and fresh texts for the new `.gen` and
every `.dsp` it names, from its tree, so a keystroke still on its way lands in
a text nobody has. Two switches at once are made one after the other, and the
room's chat says who made each. That is everyone's text, edits not yet applied
included, so it asks first. If the room is playing, the relay plays the last
of the switches made together from the top, with a start of its own, rather
than anyone applying it as an edit: `thcGenDiff` keeps no stage of one piece
in another, and an edit would start the new piece at the old one's transport
time. Stopped, the next Play loads it. A start carries a Yjs snapshot of the
document beside its hash -- every writer's clock and every delete, since a
delete moves no clock -- so a peer whose document has gone past that revision
-- a switch made just after a Play -- loads the relay's copy of it, as a late
joiner does, rather than waiting for a revision that will not come. A Stop or
a tempo names the run it was made in, and a peer or relay that has started
another since drops it. The tabs and the node editor offer only the `.gen` and
the files it names, so a pasted `.gen` that stops naming a graph hides it
without removing it, since only a switch removes files. A graph it names that
the document lacks is added from the shipped ones by whoever presses the next
Apply or Play.

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

**M7 — later, if wanted.** Web MIDI controllers mapped to knobs (notes are
in, section 5). Recording the tape and the mix.
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
