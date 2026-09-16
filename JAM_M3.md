# M3 — two tabs

The third milestone of [JAM.md](JAM.md): two browsers share one piece, edit
it together, press play once, and deliver the same tape. This document is
the detailed plan: what gets built, in what order, what it needs from M2,
what it decides that JAM.md left open, and the gates that say it is done.

The one-line shape: **a relay that holds the document and the clock, a
peer mesh that carries gestures, and a page where every input, including
its own, is a command stamped with the transport time it applies at.**

## 0. Where M3 starts from

What is in the tree on `jam-m2` (JAM.md, "M2, so far"):

- The browser build (`wasm/web/`): libthink, the 62 DSP plugins, the
  sixteen composers and the composer host in one 602 KB wasm; the
  scheduler in the worklet, stepped once per window; load, transport,
  knob and MIDI-in as frame-stamped commands on one queue; the tape
  posted back in batches with the transport's position and an epoch; a
  page with a piece mode, sliders for the knobs, a roll, and keys on
  screen. `piececheck.mjs` and `browsertest.mjs` hold every seeded piece's
  tape against `genwav.mjs`'s at four steps, in CI.
- The scheduler ticks every stage at the time it asked for, so a piece's
  tape is a function of the file and the seed and of nothing about the
  host's step. `gencheck` gates it at 20 ms, 1024 frames and 256.

This document was written before M2 landed, and section 2 said what M2's
seam would have to be. M2 built its own shape -- frame stamps, applied at
the top of the window they fall in -- and M3's first step was to add what
section 2 asks for on top of it. Section 2 now describes the seam as
built.

## 1. Decisions M3 makes

JAM.md left these open or sketched them. M3 fixes them.

**Stamps are transport seconds.** JAM.md's message sketch said beats. The
scheduler's own clock is transport seconds (`thcScheduler::now()`), a
tempo change is a transport command like any other, and integrating beats
across tempo changes is the scheduler's job, done identically on every
peer. So the wire carries `at`, in transport seconds, and beats are derived
for display. This keeps the stamp the same unit the scheduler applies it in.

**Scheduler-facing commands are applied at their stamp, inside the step.**
A window is 256 frames, 5.3 ms at 48 kHz, and two peers' windows are not
aligned to each other or to the origin. A knob applied "at the top of the
window containing t" lands at different transport times on different peers,
and a composer that reads it at a tick between those two times composes
two different pieces. The step-size fix already ticks stages at exact
times; commands to the scheduler get the same treatment: they are events
in the same ordered heap, applied at `at` before any stage ticks at or
after `at`. Only live notes in direct mode, which go to the synth and not
the scheduler, are frame-stamped, because they are not on the tape.

**Every command is sent ahead of its time.** A knob moved on one peer must
reach every other peer before the transport gets to `at`, or that peer
applies it late and the tapes part. So a sender stamps a knob at
`now + knobLead` and a transport change at `now + transportLead`, and
applies the same stamped command to its own scheduler. The sender's ear
hears the knob `knobLead` later. Starting values: knobLead 150 ms,
transportLead 500 ms, both shown in the page and both adjustable. A command
that still arrives late is applied immediately and counted, and the count
is on the page. M3 makes the hazard visible; recovery is M4's.

**Play starts at a scheduled origin.** Pressing play sends a transport
command with `origin`, a relay-clock time `transportLead` ahead. Every
peer converts origin to its own audio frame, and at the window containing
that frame starts the scheduler with a partial first step, so transport
zero is exactly the origin's frame and not the start of its window. From
there transport time is `(frame − originFrame) / rate` on every peer,
whatever its sample rate or window alignment. A peer that joins a room
already playing sees the document and hears nothing until the next play;
fast-forward into a running transport is M4.

**Applying an edit restarts the piece.** M3 shares the text live, with
cursors, but a change reaches the schedulers only when someone presses
Apply, which is stop, load, and play from a new origin, as scheduled
commands. Landing an edit at the bar with stage state kept is M4, behind
its harness. The load command names the document revision it expects
(section 4.3), so no peer loads text the CRDT has not yet delivered to it.

**The relay is authoritative for presence and seats; nothing else.** It
holds the document, answers the clock, forwards signalling, and says who
is in the room and on which seat. It never sees a note or a knob unless
the mesh fails (section 5.5).

**Direct mode only.** Quantised and play-ahead modes are M4.

## 2. The scheduler seam

M2 put the composers in the static bundle and the scheduler in the worklet,
with every command stamped with a frame and applied at the top of the
window that frame falls in. That is right for a key -- a note sounds from
the next window anyway -- and wrong for anything the scheduler reads: two
peers' windows are not aligned, so "the top of the window containing t" is
two different transport times on two peers. M3 adds the stamped commands
beside M2's, in `wasm/web/thinkweb.cpp`, and the worklet's messages and
`host.js` mirror them one for one.

**Exports**, beside M2's:

```
tw_piece_load(text, seed)
    M2's load, plus the master seed to compose from when the file pins
    none (below zero: draw one). The .dsp files a piece names are handed
    over first by tw_instrument, as M2 has it; a room hands over the
    document's.

tw_begin(originFrame)
    Arm a start from the top: at the window containing originFrame the
    scheduler is rewound and started with a partial first step, so that
    transport 0 is that frame exactly. A frame already rendered starts at
    the next window and counts as late.

tw_at(at, op, value)
    A stop or a tempo, applied at transport time `at`, inside the step.

tw_knob(at, index, value)
    A piece knob, by the index the load reported it under, at transport
    time `at`, inside the step. Below zero: the top of the next window,
    which is what the solo page sends.

tw_late()        commands applied after their time, so far
tw_origin()      the frame transport 0 falls on, or -1
tw_seed()        the master seed the piece is composing from
tw_instrument_*  the piece's instruments, with the channel each was given
tw_listens(ch)   whether a chain takes `input midi' on the channel
```

`worklet.js`'s `tape` message carries `now`, `origin`, `frame` and `late`
with the events, and its `piece` reply carries the instruments, the
channels the piece listens on, and the seed.

**Properties M3 relies on**, and how each is kept:

1. A command with `at` in the future is held and applied at `at`, after
   the stages that tick at or before `at` and before any that tick after
   it: the transport is stepped to `at` exactly (`thcScheduler::
   stepTransportTo`), the command applied, and the step carried on. A
   command with `at` in the past is applied at once and counted.
2. A load stops the transport first, and a start is a rewind. (M4 changes
   the first.)
3. Transport time is `(frame − originFrame) / sampleRate` exactly: every
   window steps *to* that time rather than *by* a window's length, so the
   clock never drifts from the frames.
4. The tape a peer's worklet delivers for a piece, from an origin, with a
   set of stamped commands, equals the tape `genwav.mjs` delivers under
   Node from the same piece and commands: `genwav.mjs -c "AT knob NAME
   VALUE"`, `-c "AT tempo BPM"`, `-c "AT stop"` apply a command the same
   way, at its time inside the step. The protocol harness (section 8.1)
   compares all three.

**A rewind is a load.** Found on the way, by the harness: `orrery`
composed a different lead after a rewind than after a load, on the
desktop as much as in the browser. A load creates each composer instance
over the plugin's defaults and then sets the file's values and makes the
bindings, each announced as it happens; a rewind re-created the instance
over the final values and announced only the bindings. `gen::evolve` draws
a population in create sized by `length` and `population` and draws
another on each of those being announced, so the two paths had drawn
different amounts of randomness before the first bar. The store now keeps
everything the loader did to it and a rewind puts it back to the defaults,
creates the instance, and does it all again. Every seeded piece now
rewinds to the tape it loaded to, and `gencheck` gates that over the
corpus -- which matters here because every peer's Play *is* a rewind of
the piece it loaded.

## 3. The relay

`wasm/relay/relay.mjs`, Node, one process, one port, no database in M3.

### 3.1 Endpoints

```
GET  /                         health, version, room count
WS   /doc/<room>               the Yjs document, y-websocket protocol
WS   /room/<room>              JSON: presence, seats, clock, signalling
```

Two sockets per peer rather than one multiplexed one: y-websocket's
framing is its own, and the JSON side is easier to read on the wire and in
a harness when it is not sharing a socket with binary CRDT updates.

### 3.2 The document socket

`y-websocket`'s server utilities as they come: `setupWSConnection` per
socket, one `Y.Doc` per room, kept in memory. Persistence is M5's problem;
in M3 a room that empties is kept for an hour and then dropped.

A new room is seeded: the relay reads a shipped piece and every `.dsp` it
names from the tree and puts them in the document (section 4.1) before the
first client attaches. The piece is named in the room URL's query, and the
default is `gen/airports.gen`.

### 3.3 The room socket

Messages, JSON, one per frame, `type` first:

```
-> hello    { peer, name, protocol }         protocol: 1
<- welcome  { peer, peers: [{peer, name, seat}], seats: {...}, piece }
<- joined   { peer, name }        <- left { peer }
-> seat     { seat }              <- seats { seat: peer, ... }   refused: seat null
-> ping     { t0 }                <- pong { t0, t1 }            t1: relay ms
-> signal   { to, data }          <- signal { from, data }      opaque
-> relayed  { data }              <- relayed { from, data }     section 5.5
```

`peer` is chosen by the relay, a short random id. Seats are first-claim:
a `seat` for a taken seat answers with the current map and no change. A
seat is released when its socket closes.

The clock is `process.hrtime.bigint()` in milliseconds as a double, never
`Date.now()`, so a step of the system clock does not move the origin.

### 3.4 Running it

```
node wasm/relay/relay.mjs [--port 8787] [--tree DIR]
node wasm/web/serve.mjs --relay ws://127.0.0.1:8787
```

`serve.mjs` grows one flag: the relay URL the page should use, written
into a `config.js` it serves. On a LAN the page still has to be opened as
`localhost` (serve.mjs's header says why), so the second machine forwards
the port; the relay itself can be reached over the LAN directly since a
WebSocket needs no secure context from a `localhost` page.

## 4. The document

### 4.1 Shape

One `Y.Doc` per room:

```
files: Y.Map<string, Y.Text>     "piece.gen", "amb01.dsp", ...
meta:  Y.Map                     { piece: "piece.gen", seeded_from: "gen/airports.gen" }
```

Every `.dsp` the piece names is a file in the map, by the name the `.gen`
uses. The loader resolves `dsp "amb01.dsp"` against the map and nowhere
else, which is also why `tw_piece_load` takes texts rather than paths. A
piece that names a `.dsp` the map lacks fails to load with that name in the
message, on every peer alike.

### 4.2 The editor

CodeMirror 6 with `y-codemirror.next`: one editor, a tab per file, remote
cursors with the peer's name. No language mode in M3 beyond comments and
strings for the shared lexer's syntax; a real mode is cheap later since
both formats share one scanner.

This is the first client dependency the site has. `esbuild` bundles
`main.js` and its imports into `build-web/` from the CMake build, as one
step after the wasm link, so the site is still one directory served
statically. `package.json` gains `yjs`, `y-websocket`, `y-codemirror.next`,
`codemirror`, and `esbuild` as a dev dependency. The worklet has no
imports and is not bundled.

### 4.3 Revisions

Apply must load the same text on every peer. A Yjs document has no global
revision number, so M3 makes one: the `load` command carries the SHA-256 of
the concatenated files, in map order, at the moment the sender pressed
Apply. A peer that receives the command hashes its own replica; if it
differs it waits for document updates, re-hashing on each, until it
matches or the command's `at` passes, in which case it is a late command
and counted. With `transportLead` at 500 ms and edits that are already on
the wire, this waits for nothing in practice, and it turns a silent
divergence into a counted one.

## 5. The mesh

### 5.1 Signalling and connection

A full mesh: every pair has one `RTCPeerConnection`. The peer with the
lexically smaller id offers. Offer, answer and ICE candidates go through
the room socket's `signal` message. STUN is a public server; TURN is not in
M3, and on a LAN or one machine it is not needed.

One thing to check on the LAN early: Chrome hides host candidates behind
mDNS names. Both machines need mDNS resolution, which on Linux is Avahi
with `nss-mdns`. If ICE fails on the LAN for that reason, section 5.5 is
the fallback and the round trip is shown, so the failure is visible, not
mysterious.

### 5.2 The gesture channel

One `RTCDataChannel` per pair:

```
{ ordered: false, maxRetransmits: 0 }
```

JSON, one command per message. Binary can come later if the size ever
matters; at a knob move every 16 ms it is under 2 KB/s per pair.

### 5.3 The commands

Every command has `at` in transport seconds, `from` the sending peer, and
`seq`, a per-peer counter, so a receiver can drop a duplicate and count a
gap. The set for M3:

```
transport  { at, op: "start", origin, piece: {hash}, seed }
transport  { at, op: "stop" }
transport  { at, op: "tempo", bpm }
knob       { at, name, value }
note       { at, seat, note, velocity }        direct mode: frame-stamped locally
noteoff    { at, seat, note }
```

`transport start` goes over the room socket as well as the mesh, since it
is the one command a peer must not miss; the relay keeps the last one and
gives it to a joining peer in `welcome`, which is how a late joiner learns
the room is playing and what it is playing.

### 5.4 Applying a command

On every peer, the sender included, the same function turns a command into
a worklet message:

- `transport start`: `origin` from relay ms to an audio frame through the
  clock map (section 6.2), then `tw_transport_start(frame)`. The piece is
  loaded first, from the document at the named hash (section 4.3).
- `transport stop|tempo`, `knob`: straight through, `at` as it is.
- `note`: `at` to a frame through the transport map. If that frame is in
  the past, which for a remote note it always is by one network delay,
  play it in the next window. That is what direct mode means.

The sender applies its own commands through the same path, so its own
knob is heard `knobLead` late, which is also what keeps its tape equal to
everyone else's.

### 5.5 When the mesh fails

If a pair's data channel has not opened within 10 s, or drops, both sides
route their gestures to each other through the room socket's `relayed`
message. Same commands, same stamps, one more hop. The page shows which
peers are direct and which are relayed. This is needed later anyway for
rooms past six peers, and building it now means a LAN with awkward ICE
still gives a tape to compare.

## 6. Clocks

Three clocks, two maps between them.

### 6.1 Relay clock

The page pings the room socket once a second with `t0 = performance.now()`
and gets `t1`, the relay's ms. Offset `= t1 − (t0 + t2) / 2`, RTT
`= t2 − t0`, for the reply at `t2`. Keep the last 16 samples and use the
offset of the one with the smallest RTT; that is NTP's idea and it
discards the jittered ones. Show RTT and the spread of the kept offsets on
the page. Relay time now is `performance.now() + offset`.

A background tab throttles this timer to once a second or worse. Once a
second is what it runs at anyway, and a WebRTC connection exempts the tab
in Chrome; if the tab is throttled harder the offset goes stale and the
spread shows it.

### 6.2 Audio clock

`AudioContext.getOutputTimestamp()` returns `{ contextTime,
performanceTime }`, a pair sampled together: the frame the output is at,
and the `performance.now()` that was. Sample it every second alongside the
ping and keep a linear fit of `contextTime` against `performanceTime` from
the last 16, since the audio clock and the wall clock drift apart by tens
of parts per million. Then

```
frameOf(relayMs) = (fit(relayMs − offset)) × sampleRate
```

is how an origin becomes a frame, and it is re-evaluated only when a
`transport start` arrives; after that, transport time is frames from the
origin and no wall clock is consulted again until the next start.

### 6.3 Transport clock

Transport seconds since origin, owned by the worklet and reported in
every `tape` message. The page keeps `transportNow()` as the last reported
value plus `ctx.currentTime` elapsed since, which is exact between
reports because both are the audio clock. Stamps are made from it.

## 7. The page

`main.js` grows, and probably splits, into:

```
room.js       the room socket: hello, presence, seats, ping, signal
mesh.js       RTCPeerConnections, data channels, the relayed fallback
clock.js      the two maps from section 6
doc.js        the Y.Doc, the file map, the hash, the editor binding
commands.js   make a command; apply a command (section 5.4); the late count
host.js       as now, plus the M2 messages
main.js       the UI
```

What the page shows in M3, and nothing more:

- **Room and peers.** The room name, who is here, their seat, and for each
  peer whether the gesture path is direct or relayed and its RTT.
- **Seat picker.** The piece's instruments by name, with the channel each
  has; claim one. Keys play into your seat's channel, on every peer.
- **Transport.** Play, stop, tempo, and the transport time. Play is
  disabled until the clock has at least four samples.
- **Knobs.** The piece's `@knobs` as sliders, driven through commands.
- **Editor.** The tabs, the cursors, Apply.
- **The numbers.** Relay RTT and offset spread; `knobLead` and
  `transportLead`, editable; the late-command count; the browser's own
  latency figures from M1.
- **Tape export.** A button that downloads the tape delivered so far, in
  `genwav`'s tape spelling, for section 8's compare.

## 8. Gates

Three, in increasing order of what they need, and the first two run in CI.

### 8.1 The protocol harness (no browser)

`wasm/web/protocoltest.mjs`. Two peers in one Node process, built from the
same `clock.js` and `commands.js` the page uses, a simulated network with
delay, jitter and loss, a simulated relay with a clock of its own, and for
each peer the browser module itself -- the same wasm the worklet runs --
stepped at that peer's window and rate, 256 at 48 kHz for one and 1024 at
44.1 kHz for the other, their blocks out of phase. A script presses play,
moves a knob from each side at times of its own, changes the tempo from
each side, and stops. The two tapes are diffed against each other and
against `genwav.mjs`'s under the same command stream.

**Passes**, and is in the CI `wasm` job: the tapes are identical for every
seeded piece with 40 ms of delay, 20 ms of jitter and 2% loss, no command
is late, and each peer's origin frame is within the jitter of where the
origin truly fell on its output. And with the delay raised to 300 ms, past
`knobLead`, the knobs arrive late, both peers count them, and the page's
own reckoning names which. The second half is a test that the hazard is
seen, not hidden.

This harness is also JAM.md section 5's "two schedulers in one process",
which M4 extends with edits at scripted beats. It is built first, because
everything else in M3 is plumbing around what it proves.

### 8.2 Two headless browsers (needs M2)

`browsertest.mjs` gains a second scenario: start a relay, open a Chromium
page and a Firefox page on it, both with a live `AudioContext`, one
presses play, both run a seeded piece for 30 s while the script moves a
knob from each side, then both export the tape. Passes when the tapes are
identical to each other and to `genwav.mjs`'s for the same piece and the
same command stream, and the late count on both is zero.

Cross-browser on purpose: a Chromium peer and a Firefox peer is the
wasm-against-wasm gate from M2 with the network in between.

### 8.3 Two machines on a LAN (by hand)

Two laptops, one relay, the page on each, sound cards at whatever rate
they run. Play `airports.gen` for three minutes, each side moving a knob
and playing a few keys into its seat, then export both tapes and diff
them. Done when they match, the late count is zero, the shown RTT is a
number that looks like a LAN, and both people heard the other's keys.

## 9. Order of work

1. **Protocol harness scaffolding** (8.1), with the Node module: fake
   clock, fake mesh, `commands.js`, `clock.js`. Proves the stamping and
   application rules before any UI exists.
2. **The relay** (3): clock, presence, seats, signalling, `relayed`, and
   the document socket with room seeding.
3. **The document and editor** (4): Y.Doc, tabs, cursors, hash, esbuild in
   the build.
4. **The mesh** (5): connect, channel, fallback, RTT display.
5. **Clocks in the page** (6): both maps, the numbers on the page.
6. **The scheduler seam** (2): when M2 lands, the M2 messages in
   `host.js`, and the stub replaced. If M2 is still in progress at this
   point, the seam's shape is already fixed by section 2 and the stub
   answers the messages.
7. **The page** (7): seats, transport, knobs, tape export.
8. **Gates 8.2 and 8.3.**

Steps 1 to 5 need nothing from M2 and can run in parallel with it. Step 6
is the join.

## 10. Risks particular to M3

1. **Commands applied at window boundaries rather than at their stamp.**
   The tapes would agree on one machine and part on two. Section 2's first
   property is the guard, and 8.1 with two different windows and rates is
   the test.
2. **The audio-clock fit.** A bad fit puts the origin a few ms off on one
   peer, and every note from then on is off by that much. The tapes still
   match, since transport time is frames from the origin, but the peers are
   out of time with each other by ear. Show the fit's residual; if it is
   above a millisecond, take more samples before allowing play.
3. **`performance.now()` resolution.** Firefox coarsens it to 100 µs, or
   to 1 ms with fingerprinting resistance on. Both are under a window.
4. **The Yjs replica lagging at Apply.** Section 4.3 turns it into a wait
   and then a counted late command rather than two peers playing two
   pieces.
5. **mDNS ICE on a LAN.** Section 5.1; the fallback in 5.5 means the gate
   still runs.
6. **Firefox's 10 ms worklet message batching**, unmeasured on hardware.
   It bounds how fresh `transportNow()` is, and a stamp made from a stale
   value is early, never late. Harmless to the tape; it may make
   `knobLead` feel longer on Firefox.
7. **The relay as a single point.** Fine for M3, whose relay runs on one
   of the two machines. Deployment and persistence are M5.

## 11. Not in M3

- Fast-forward into a running transport, edits at the bar, quantised and
  play-ahead modes: M4.
- Persistence, deployment, TURN, rooms past six peers: M5.
- Any composer view beyond knobs and the tape: M6.
- Voice: M7.
