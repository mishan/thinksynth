# Where the composer scheduler runs in the browser

M2 of [JAM.md](JAM.md) puts a piece in a tab: the `.gen` loader, the
composer scheduler, knobs as sliders, and a gate that the tape the browser
delivers matches the tape `genwav.mjs` delivers. JAM.md section 3 decided
that the scheduler runs on the main thread. Before building that, this
document sets out what the scheduler actually does to the synth, measures
what each placement would cost, and records a finding that constrains the
jam whichever placement is chosen.

Short version:

- **The scheduler's tape currently depends on its step size.** Two peers
  whose sound cards run at 44.1 and 48 kHz would compose different pieces
  from the same file and seed. A two-part change to `runDueTicks` fixes
  that for all 13 seeded pieces. It is needed for the jam whichever
  thread the scheduler runs on, and it changes what the existing pieces
  compose.
- **On the main thread the scheduler needs a bridge.** It is wired to a
  `thSynth` object, not to an interface, and in the browser the synth is
  in another wasm instance. Everything it does to the synth -- more of it
  than the tape shows -- would have to be forwarded as stamped messages
  and re-applied in the worklet, and a second, non-rendering synth kept on
  the main thread so the scheduler has something to hold.
- **In the worklet it needs no bridge.** It drives the real synth the way
  `genwav` does. That costs audio-thread time -- measured, and small -- and
  it breaks the composer ABI's single-thread promise for the (future)
  visualizers.
- **Recommendation:** make the step-size fix first, then run the scheduler
  in the worklet. The reasoning, and what would change it, is in the last
  section.

## 1. The options

**A. Main thread (JAM.md's plan).** The scheduler, the composers, the `.gen`
loader and the control-rate node host run on the page's main thread, in
their own wasm instance. They are stepped from the audio clock, ahead of it
by a lookahead, and everything they deliver is posted to the worklet as a
command stamped with the frame it applies at (M1 built the stamps).

**A′. A dedicated Worker.** A, with the scheduler in a worker, not the main
thread, talking to the worklet over a `MessageChannel`. Same bridge, same
second synth; different timers (section 4.6).

**B. The worklet.** The scheduler runs beside the synth in the worklet's
instance and is stepped once per synth window, exactly as `genwav` steps it.
The main thread sends the `.gen`, knob moves and transport commands, and
receives the delivered events (the tape) for display.

## 2. What the scheduler does to the synth

`thcScheduler` holds a `thSynth *` and calls it directly. Every call, from
`src/thcScheduler.cpp` and `src/thcNodeHost.cpp`:

| What | How | On the tape? |
|---|---|---|
| A note | `deliver()` → `addNote` | yes (`N`, with its duration) |
| Its note-off | the scheduler's own `noteOffs_` heap → `delNote` when due | **no** |
| A chanarg event | `deliver()` → `getChanArg(...)->setValue` | yes (`C`) |
| An instrument swap | `deliver()` → `swapInstrument` → `loadTree`, `writeValues`, `removeChan` | yes (`P`), but not what it loads |
| A node-arg edit | `deliver()` → `setNodeArg` → edits the channel's prototype graph through `getChannel()->modnode()` | yes (`E`) |
| A knob that drives a chanarg | a `signal_arg_changed` connection that calls `setValue` on the synth's chanarg directly, folding units by `getSampleRate()` | **no** |
| Instruments at load and at rewind | `applyInstrument` → `loadTree`, `writeValues` | **no** |
| Stop, rewind | `flushNoteOffs`, `flushHeld` → `delNote` for everything sounding | **no** |
| The control-rate node host | a second `thSynth` (window 1, 50 Hz) built on the first one's plugin path, so every DSP plugin loads wherever the scheduler is | — |

Two further facts about the composers:

- **None of them reads the audio.** A composer sees events -- from its
  chain, or from live MIDI (`composer_receive`) -- and the UI
  (`composer_input`, `composer_capture`). Nothing a composer does depends
  on rendered sound, so a synth that renders nothing would not starve one.
- **The ABI promises a single thread.** `thcomposer.h`: "The entire
  composition layer lives on the GUI thread ... tick/receive/param access
  and composer_draw never run concurrently, so instance state needs no
  locks." The tier-two visualizers (`composer_draw`, and `composer_input`
  and `composer_capture` behind them) read instance state directly.

The desktop app is option A without stamps: the scheduler runs on the GUI
thread on a 20 ms Glib timer, stepping by the wall-clock time since the last
tick, and its calls reach the audio thread through the synth's command
queue.

## 3. The step-size finding

### What happens

`runDueTicks(now)` hands every composer it wakes a `thcTransport` whose
`now` is the end of the current step, not the time the composer asked to be
woken at. 13 of the 16 composers stamp what they emit with `t->now` and ask
for their next wake at `t->now + period`; the other three (`harmonize`,
`humanize`, `quantize`) are pure transformers. So every wake is late by up
to one step, the next wake is scheduled from the late one, and the lateness
compounds. What a piece composes is a function of the step size.

Measured over the 13 seeded pieces (60 s each, Node wasm build), comparing
genwav's step (1024 frames at 44.1 kHz) with four others:

| | 256 at 48 kHz | 1024 at 48 kHz | jittered, 2–60 ms | 1/200 s |
|---|---|---|---|---|
| pieces whose tape is unchanged | 1 of 13 | 1 of 13 | 1 of 13 | 1 of 13 |

The one is `hands`, which emits nothing in its first minute. The others
differ in timing and, for most, in content:

- `airports`: the same 26 notes, shifted by up to 62 ms at 256/48k and up
  to 79 ms at 1/200 s after a minute.
- `colony`: 224 events at genwav's step, 145 at 1024/48k.
- `orrery`: 753 events at genwav's step, 794 at 256/48k, 807 at 1/200 s.
- `loom`: 940, 962 at 256/48k, 904 jittered.

### Why it matters

- **For the jam, whichever placement.** A peer's synth window and sample
  rate are its hardware's. Two peers at 44.1 and 48 kHz stepping the
  scheduler once per window would compose different pieces from one file
  and one seed -- the silent desynchronisation JAM.md section 1 says
  determinism exists to prevent.
- **For the M2 gate as written.** The browser tape matches `genwav.mjs`'s
  only if the browser steps by exactly genwav's step.
- **On the desktop, today.** Live playback steps by wall-clock time on a
  20 ms timer, so two live plays of a seeded piece are not the same tape
  either. Only `gencheck` and `genwav`, which step a fixed virtual clock,
  repeat.

### The fix

Two changes to `runDueTicks`, measured in a scratch worktree:

- **v1:** tick each composer at its own wake time. `t.now = w.at`, and
  `t.beat` back-dated by the same interval.
  - Result: 10 of 13 pieces invariant.
  - Still differing: `breath`, `ebb` and `round`. Their stage params read
    control-rate nodes, which had already been stepped to the step's end.
- **v2:** v1, and step the chain's nodes to the wake time before the tick.
  The step to the step's end moves to after the ticks.
  - Result: **13 of 13 pieces invariant** across all four step sizes,
    jittered included.

With v2, a piece's tape is a function of the file and the seed, not of the
sample rate, the window, or the timer.

v2 changes what the existing pieces compose, as the determinism fix did.
Events per piece in 60 s, at genwav's step:

| | airports | bloom | breath | colony | ebb | loom | loosen | orrery | reshape | round | tide | weather |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| now | 26 | 91 | 24 | 224 | 45 | 940 | 486 | 753 | 21 | 492 | 2219 | 72 |
| v2 | 26 | 95 | 20 | 192 | 42 | 971 | 494 | 813 | 21 | 505 | 2248 | 73 |

Not yet done for v2: `gencheck` and the rest of ctest, a listen to what
changed, and a look at the one thing v1 alters that a composer might rely
on -- a wake returned at or before `t->now` is now pushed to
`t.now + 0.001` rather than `now + 0.001`.

## 4. Measurements

All from the current tree unless marked. The Node figures come from the
Node wasm build (`wasm/`); the browser figures from headless Chromium 153
and Firefox 155 on Linux (Playwright), with a live `AudioContext` and no
real sound card.

### 4.1 Scheduler cost

| | value |
|---|---|
| loading a `.gen` (plugins, instruments, chains) | 27–67 ms |
| one scheduler step, p99 | ≤ 0.05 ms |
| the slowest step, always the first (stages created) | 0.13–0.88 ms |
| fast-forward, 180 s of transport, scheduler only | 4–38 ms |

The budget for one 128-frame quantum at 48 kHz is 2.67 ms. A `.gen` load is
ten to twenty-five quanta. That is too long for the audio thread while it
is playing, and trivial before audio starts.

### 4.2 Synth cost (the same in every option)

| | value |
|---|---|
| a 1024-frame window, p50, by piece | 0.002–0.85 ms |
| a 128-frame quantum with a note-on in it, by patch | 0.02–0.38 ms |

In every option the worklet builds each note's graph (`addNote`) on the
audio thread. Moving the scheduler does not move that cost.

### 4.3 Messages between main thread and worklet

Round trip, main → worklet → main, 200–300 pings:

| | p50 | p95 | a burst of 20 |
|---|---|---|---|
| Chromium, `interactive` and `playback` | ≤ 0.1 ms | 0.1 ms | answered within 0.3 ms |
| Firefox, `interactive` and `playback` | 11 ms | 11–12 ms | all 20 answered together at 10–11 ms |

Firefox hands the worklet its messages in batches roughly every 10 ms,
whatever the latency hint. This is headless, on Linux, and needs measuring
on real hardware. It bounds how late a stamped command can arrive, and so
the lookahead A needs. In B it affects only how quickly a knob move is
heard and how quickly the page sees the tape.

### 4.4 A synth that never renders

Stepping the scheduler without rendering filled the synth's command queue:

- `TH_COMMAND_QUEUE_SIZE` is 1024.
- About 11,000 `command queue full, dropping command` messages over a
  few three-minute fast-forwards.
- One instrument swap failed ("'ts1.dsp' did not load").

`addNote` also builds and allocates the note's graph before it queues it,
and only the audio side retires it. A's main-thread synth would have to be
processed regularly -- rendered, at some window -- to stay usable.

### 4.5 Timer throttling in background tabs

- **Chrome** throttles background timers to about once a second, and with
  intensive throttling (Chrome 88+) to once a minute. Intensive throttling
  applies after 5 minutes hidden and 30 s silent.
  - Tabs playing audible audio are exempt, for a few seconds after the
    sound stops. Silent audio does not count.
  - Pages holding WebRTC or WebSocket connections are also exempt.
- **Firefox** likewise exempts tabs that are playing audio.
- **Dedicated workers'** timers are reported not to be throttled like the
  main thread's. That comes from secondary sources and has not been
  verified here.

A piece with a long silence in a background tab could stall a main-thread
scheduler (A). M3's WebRTC connection would exempt it in Chrome. B's
scheduler runs on the audio thread's clock and is not subject to timers at
all.

## 5. The options, against the evidence

### A. Main thread

For:

- It is the plan. JAM.md section 3 is written around it, and M3's network
  code, M4's apply-at-bar edits and late-join fast-forward all sit next to
  the scheduler.
- The worklet stays lean. Composer ticks and `.gen` loads never touch the
  audio thread.
- The composer ABI's threading promise holds. Draw, input and capture can
  run beside tick on one thread, as on the desktop.
- Debugging, logging and devtools are at their best on the main thread.
- With the v2 fix, jittered main-thread steps produce the genwav tape
  exactly (measured), so the tape gate is reachable.

Against:

- **The bridge is most of the work, and most of it is off the tape.**
  - **Needs forwarding:** notes, their note-offs (re-derived from
    durations), chanarg events, and swaps and node-arg edits. The worklet
    needs the instrument table, the `.dsp` texts, and the same
    sample-rate unit folding for those.
  - **Needs hooking at the source:** knob-driven chanarg writes,
    instrument application at load and rewind, and stop/rewind flushes.
    None of these appears in `sigDelivered`.
  - **Not visible to the gate.** A bridge bug leaves the tape correct and
    the audio wrong, so M2's tape gate would not see it. An audio gate
    would be needed too.
- **Two synth states that must agree.** The main-thread mirror carries the
  chanarg values, the loaded graphs and the node-arg edits the scheduler
  reads back (`chanArgExists`, `chanArgRange`, knob binding). The worklet's
  synth must match it.
- **A second synth, rendering for nothing.** The mirror has to be
  processed or it drops commands (section 4.4), and the node host needs
  its own synth besides. Every DSP plugin loads on both threads.
- **Timing depends on the message path.** Commands must be posted ahead
  of the audio by more than the worst message delay -- about 10 ms batches
  in headless Firefox -- plus main-thread jitter. A knob moved on the page
  is heard a lookahead later.
- **Background throttling can stall it during silence** (section 4.5).

### A′. A dedicated Worker

As A, with the scheduler off the main thread: timers not throttled like the
main thread's, a `MessageChannel` straight to the worklet, a free main
thread. The bridge, the mirror and the two synth states are unchanged, and
it adds a third context to coordinate.

### B. The worklet

For:

- **No bridge, no mirror, one synth.** The scheduler calls the real synth
  exactly as it does in `genwav` and on the desktop. Everything in the
  table in section 2 just works.
- **Exact timing.** The scheduler is stepped by the audio clock itself, a
  window at a time, so there is no lookahead and nothing to post ahead.
  Nothing it does is subject to timer throttling or message delay.
- **The M2 gate follows by construction.** With the same step, the browser
  runs genwav's loop. With v2, any step gives the same tape.
- **The cost is small** (section 4.1). A step is at most 0.05 ms at p99
  against a 2.67 ms quantum. The first step, up to 0.88 ms, and the `.gen`
  load both happen before audio starts.

Against:

- **The composer ABI's single thread splits.** Ticks run in the worklet
  and any UI runs on the page. Draw, input and capture would go through
  messages, with state snapshots. M2 only needs sliders, which are
  messages anyway, but a browser composer view later would pay for this.
- **A reload while playing.** Loading a `.gen` must suspend audio (or
  pre-roll), as M1's patch reload already does.
- **The audio thread does more.** Composer ticks allocate, and a burst of
  events is a burst of work in one quantum. The per-note graph build is
  already there in every option.
- **The page learns things by message.** Edits, knob gestures and transport
  from the network are messages to the worklet. The tape and the transport
  position come back by message: about 10 ms batches in Firefox, fine for
  display.
- **It amends JAM.md section 3,** and M3/M4 put their scheduler-facing
  code on the far side of a message port.

## 6. Recommendation

1. **Make the v2 fix first, whichever placement.** Without it the jam
   cannot keep peers on different hardware in step. With it, the M2 gate
   can compare against genwav at any step, and the desktop's live playback
   repeats. It changes existing pieces, so it is its own PR, with
   `gencheck`, ctest and a listen.
2. **Run the scheduler in the worklet (B).** The deciding facts:
   - **A's bridge** has to reproduce, off the tape, everything in section
     2's table that is marked "no". A mistake there is invisible to the
     tape gate.
   - **A's two synth states** must be kept identical, and the main-thread
     one has to render to stay usable.
   - **B's costs** are measured and small: a step is at most 0.05 ms at
     p99, and the one-off costs fall before audio starts.
   - **B's main drawback**, the split ABI thread, bites only when there is
     a browser composer view. JAM.md lists the node canvas under "later,
     if wanted".

What would change this:

- **A browser composer view becomes a near-term goal.** Then the
  single-thread ABI weighs more, and A′ with a proper bridge is the better
  base.
- **Real hardware shows the worklet has no headroom.** Composer bursts
  plus note builds overrunning a quantum on a slow machine would weigh
  toward A′.
- **Firefox's message batching turns out to be much worse on real
  devices.** That would hurt A and A′ -- lookahead, knob latency -- more
  than B.

## 7. Still to measure, on real hardware

- Firefox's (and Chrome's) main → worklet delay on each OS, with a real
  output device.
- The worst quantum on a slow machine: the busiest piece (`orrery`, `tide`)
  plus a chord of note-ons, in the worklet, at 256 and 128.
- Background-tab behaviour through a silent stretch of a piece, in both
  browsers (only matters for A).
- Whether a dedicated worker's timers really are unthrottled in the
  browsers the jam targets (only matters for A′).

## How the numbers were taken

- **Step-size invariance:** a Node script renders each seeded piece for
  60 s through the Node wasm build (`thinkwasm.cpp`'s `tw_step`,
  `tw_process` and event queue) at the listed step sizes, and compares the
  delivered events field for field. The jitter was a seeded LCG, uniform
  over 2–60 ms. v1 and v2 were applied to `src/thcScheduler.cpp` in a
  scratch worktree, as described in section 3, and measured the same way.
- **Costs:** the same builds, timed with `performance.now()` around
  `tw_open`/`tw_load`, each `tw_step`, and each `tw_process`. Fast-forward
  is `tw_step(1/200)` to 180 s with no rendering.
- **Note-on cost:** the browser module (`wasm/web`) under Node, a
  128-frame render with and without a note-on in it, 200 times per shipped
  patch, medians.
- **Message latency:** `host.js`'s `flush()` (a ping the worklet answers),
  sequentially and in a burst of 20, with `latencyHint` `interactive` and
  `playback`.

## Sources

- [Background tabs in Chrome 57](https://developer.chrome.com/blog/background_tabs) -- budget-based throttling; audible audio and real-time connections exempt
- [Heavy throttling of chained JS timers beginning in Chrome 88](https://developer.chrome.com/blog/timer-throttling-in-chrome-88) -- intensive throttling
- [Intent to Implement and Ship: Intensive throttling of JavaScript timer wake ups](https://groups.google.com/a/chromium.org/g/blink-dev/c/8En_5DqV_fU/m/I8e9vaecAgAJ)
- [Mozilla bug 1336484: Don't throttle timeouts in background tabs that are playing audio](https://bugzilla.mozilla.org/show_bug.cgi?id=1336484)
- [Mozilla bug 1181073: Relax the setTimeout throttling for background tabs playing Web Audio](https://bugzilla.mozilla.org/show_bug.cgi?id=1181073)
- [Mozilla bug 1291741: Relax setTimeout throttling when an AudioContext is present](https://bugzilla.mozilla.org/show_bug.cgi?id=1291741)
- Worker timers not throttled like the main thread's: secondary sources only, e.g. [Overcoming browser throttling of setInterval executions](https://medium.com/@adithyaviswam/overcoming-browser-throttling-of-setinterval-executions-45387853a826); unverified.

## 9. Decision

Taken after review, with a browser composer view and node editor now
near-term goals rather than "later, if wanted": **the v2 fix first, then
B**, and the composer view built as a mirror scheduler fed the same stamped
commands the worklet gets. [JAM.md](JAM.md) sections 3 and 3a carry the
design; this section records why the near-term view did not flip the
recommendation, and what the review corrected here.

Why the view does not flip it:

- `composer_draw` takes a `cairo_t`, and there is no cairo in a wasm main
  thread either. Draw ports unchanged only with cairo compiled to wasm, and
  then it runs as well against a mirror instance as against the authoritative
  one. Otherwise it is a snapshot export drawn in JavaScript, which crosses
  a port for nothing. Either way the cost is the same under A′ and B.
- `composer_input` is a stamped, networked command in the jam whichever
  thread ticks: the ABI's own determinism paragraph says a replay is exact
  only if the inputs are, and two peers applying a click at different beats
  diverge. So even A′ loses direct-touch input. Section 2's "single thread"
  promise still holds in B for tick, receive, param and input, because
  commands are applied at the top of a step from the queue.
- The window's other reads are already command-shaped: a few commands, a
  status snapshot, the delivered stream. Chains, instruments and sinks come
  from the document.
- The node editor edits the `.dsp` document and reads probes from the
  worklet. It does not touch the scheduler at all.
- A's bridge forwards effects, most of them off the tape. A mirror forwards
  inputs, all already stamped. Forwarding inputs is strictly less, and the
  mirror is most of A′ if the worklet ever runs out of headroom.

Corrections to the sections above:

1. "A reload while playing" (section 5, against B) applies to every option.
   The worklet parses the `.dsp` texts itself in A too. B adds only stage
   creation, at most 0.88 ms measured. Section 4.1's 27–67 ms load mixes
   side-module `dlopen` (absent in the static browser bundle), instrument
   loading (every option) and chain building (B only); it wants splitting.
2. v1's re-arm change needs a bound. A wake at or before now is pushed to
   the wake time plus 1 ms rather than the step end plus 1 ms, so a
   composer that keeps returning its own wake time ran once per step and
   can now run up to a thousand times per second of step. Cap iterations.
3. Section 4.4's full command queue is the reason a mirror synth needs a
   drain-without-render mode, applying commands and skipping DSP. Small
   engine change; it is what makes the mirror viable rather than an
   argument against any second instance.
4. Fast-forward in B must suppress delivery, or late join builds thousands
   of note graphs on the audio thread for notes that never sound.
5. Section 4.5 cuts both ways: the clock-sync pings and the Yjs provider
   live on the main thread in every option. Once a second is enough for
   offset estimation, and WebRTC exempts the tab in Chrome anyway. Not a
   differentiator.
6. `thcomposer.h`'s "same thread as tick" comment on `composer_draw`
   becomes "same thread as tick, or a mirror fed the same inputs".
