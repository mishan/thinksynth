# Composer framework — handoff

Handoff for implementing thinksynth's algorithmic-composition framework.
The design phase is done: four layers sketched, decisions argued, open
questions flagged. This doc is what a fresh session needs to start writing
real code without relitigating anything by accident.

Repo: github.com/mishan/thinksynth (C++, gtkmm-4, CMake, GPL-2+).

> **STATUS — August 2026.** All six milestones of §6 are landed. The
> sketches in §4's inventory are real files at their destinations, the
> tree builds, and the ctest suite (including the new `gencheck` gate)
> passes. `gen/airports.gen` loads and plays: eight eno lines, the
> wildcard through `xform::quantize`, and the drift chain driving a
> chanarg sink through `gen::walk`. The launch plugin set is eno_line,
> walk, quantize, humanize, euclid — euclid carries the first
> `composer_draw` (the onset ring), hosted in ComposerWindow's draw
> strip. Piece `@knobs` are `thArg`s with their .dsp metadata, shown as
> live sliders. Replay determinism is a ctest gate: `scripts/gencheck`
> renders the piece twice through the scheduler's virtual clock
> (`stepTransport`) and diffs the delivered streams byte for byte, and
> also holds down the pitch parser and ten loader-rejection cases.
>
> Deltas from the sketches and the original plan worth knowing:
>
> - **The `.gen` parser is hand-rolled in `src/thcGenFile.cpp`**, not
>   bison/flex additions to thinklang.yy as §5 sketched. Deliberate:
>   the shared grammar is non-reentrant, folds `ms` into *samples*
>   (engine semantics a .gen value must not inherit), and cannot see
>   thcPlugin/thcScheduler from libthink. The lexical layer is the .dsp
>   one reproduced faithfully instead of linked; GEN_FORMAT.md is
>   implemented as written, including "a file with any error loads
>   nothing" and name-and-line messages.
> - **`thcParamDef` grew a `units` field** ("s" marks a duration) so the
>   loader can enforce §2 of the format and do the beats→seconds
>   conversion at read time (`thcParamStore::setBeats`). Interface
>   version stays 1 — nothing built against the old struct exists
>   outside this tree.
> - **Sinks route and rewrite.** Events leaving a chain are fanned out
>   per sink, the sink's channel overwrites the event's, and a chanarg
>   sink names the target arg. eno_line's `channel` param is gone:
>   routing belongs to the piece, not the plugin.
> - **NOTESET params arrive resolved** — "53,56,60", parsed once by
>   `thcGenLoader::parseNoteList`; eno_line's placeholder note-name
>   parser is deleted as planned.
>
> - **Velocity is raw 0–127 through the whole path.** `thSynth::addNote`
>   takes what `dispatchmidi` passes from the wire and what the Keyboard
>   widget passes from its velocity rows; the sketch's `/127.0f` was
>   wrong and is gone.
> - **Note-offs are `delNote`**, not `releaseNote`; chanarg delivery is
>   `getChanArg(chan, name)->setValue(v)` — the slider route, not
>   `setChanArg` (which takes a built `thArg*` and queues a swap).
> - **`thcPlugin` and `thcScheduler` live in `src/` on purpose**, as the
>   object library `think_composerhost` (glibmm, no gtkmm) — parallel to
>   `think_visualhost`. libthink links only sigc++ and stays
>   main-loop-free; the scheduler needs the Glib loop. Only the ABI
>   header `thcomposer.h` is in libthink. The milestone-5 harness should
>   link `think_composerhost` headlessly, the way the smoke tests did.
> - **The heaps are vectors + `std::push_heap`**, not `priority_queue`,
>   because `peekPending()` has to iterate pending_. A queued chanarg
>   name is copied into the Pending record (the ABI's "copied by the
>   sink" promise; the emitting composer may rewrite its string).
> - **The hardcoded piece** is `ComposerWindow::buildAirports()` —
>   the airports.gen table minus the two chains whose plugins don't
>   exist yet. It is the thing milestone 4 deletes.
> - New code standardizes on American English spelling (color, not
>   colour).

## 1. What is being built

A generative-music sandbox inside thinksynth: composition algorithms as
plugins (generators and transformers), chained into pipelines described by a
`.gen` file, scheduled against a transport, delivered into the existing
synth as notes and `@chanarg` changes, and visualized on a scrolling piano
roll that shows the *scheduled future* as well as the delivered past. First
target style: Eno-ish ambient (free-running lines, incommensurate periods),
but the framework is deliberately general — Euclidean, Markov, L-systems,
CA all fit the same interface.

A working python prototype exists (`genplay.py` + `airports.gen` v1): a
standalone MIDI sender whose semantics the `eno_line` plugin reproduces
1:1. Useful as executable documentation and for A/B-ing the native layer.

## 2. Repo context that constrains everything

Read `ARCHITECTURE.md`, `DSP_FORMAT.md`, `VISUALIZERS.md` first. The facts
this design leans on:

- **Two threads, one bridge.** GUI thread and audio callback, communicating
  through a swap-based command queue. MIDI input arrives on an RtMidi
  thread and hops to the GUI thread via a Glib Dispatcher; the GUI thread
  does all allocation and enqueues commands; `process()` drains them at
  window boundaries. The framework lives ENTIRELY on the GUI thread and
  touches the synth only through the same calls the on-screen keyboard
  uses (`addNote` / `releaseNote` / chanarg set). Nothing in this design
  adds a thread or a lock.
- **Plugins are dlopen'd shared objects** registering named args through a
  host-provided function that returns integer indices (`thPlugin`,
  `module_init`). The composer API mirrors this shape on purpose.
- **The `.dsp` language** is bison/flex (`libthink/thinklang.yy`,
  `thinklex.ll`): `#` comments, `;`, `::`, `{}` blocks, quoted strings,
  numeric literals with units (`ms` exists), and `@chanarg` declarations
  whose `.widget/.min/.max/.label/.units/.group` metadata IS the control
  panel. `.gen` v2 reuses this lexical layer; it needs only `s` and
  `beats`/`b` as new unit tokens.
- **Visualizers** (`plugins/visual/`, cairo) solve a GUI-vs-audio-thread
  problem with probes and snapshots. The piano roll explicitly does NOT
  need that machinery — same-thread producer and consumer.
- Timing granularity today is window-quantized (commands apply at the top
  of `process()`); accepted as fine for v1, sample-accurate timestamped
  commands is a known, contained future upgrade.

## 3. The four layers and their load-bearing decisions

### 3a. Plugin API — `thcomposer.h` (+ `eno_line.cpp` as proof)

- One interface, two roles: generators export `composer_tick`, transformers
  export `composer_receive`, a plugin may export both (arpeggiator).
- **Pull scheduling**: `tick()` returns the absolute time of its next
  wanted wakeup; the host keeps one priority queue of wakeups instead of
  polling. `THC_NEVER` sleeps until `composer_param_changed` re-arms.
- **Events carry duration, not note-offs.** The scheduler derives offs; no
  plugin tracks hanging notes. Events carry absolute transport seconds and
  may be emitted arbitrarily far into the future.
- Event types: `THC_EV_NOTE` and `THC_EV_CHANARG` (generative timbre is a
  first-class citizen, not an afterthought).
- Params registered once with full metadata (`thcParamDef`) — this
  deliberately forces building the arg-metadata layer the parameter panel
  wants anyway. Types include `NOTESET`/`STRING`; see §5 for how NOTESET
  resolution was settled.
- Determinism contract: all randomness from the per-instance seed in
  `thcParams`, never global state.
- `composer_draw(state, cairo_t*, w, h)` is the optional tier-two
  visualizer (Euclid ring, CA grid…); it reads instance state directly —
  legal because everything is GUI-thread.

### 3b. Scheduler — `thcScheduler.h` / `thcScheduler.cpp`

- **Synchronous propagation, scheduled delivery.** When a stage emits, the
  event runs through the remaining stages of its chain immediately as
  function calls; only what exits the chain enters the `pending_` heap
  keyed by `at`. This makes replays deterministic and cycles
  unconstructible. Echo = emit future copies; humanize = adjust `at`;
  neither needs a clock.
- **Integrated transport.** `transportNow_` and `beat_` accumulate deltas
  from `g_get_monotonic_time`; tempo changes just change the beat slope, so
  beat-valued durations survive tempo automation. All heaps keyed in
  transport time → pause freezes everything coherently.
- Driven by one ~20ms Glib timeout. It keeps firing while paused (draw
  stays live; derived note-offs drain) but the musical clock freezes.
- `stop()` flushes all pending note-offs immediately — a pause never hangs
  a note. `reset()` destroys and recreates every instance with the same
  seeds: same `.gen` + same master seed = same piece, which is also the
  test story (render twice, diff delivered event streams, ctest gate — same
  spirit as the existing `dsplevel` corpus sweep).
- Mute drops at end-of-chain, not at source: algorithms keep evolving
  silently so un-muting mid-piece rejoins a living process.

### 3c. File format — `GEN_FORMAT.md` (+ `airports2.gen` example)

- Shaped like `.dsp`, flat like `.patch` where it can be. `name`/`author`/
  `description` info strings; `tempo`; `seed` (present iff user pinned it).
- **Units decide the clock**: `period = 19.4 s` free-running, `= 4 beats`
  clocked — in the value, not the plugin. Bare numbers on duration params
  are a load error.
- **Piece knobs are chanargs**: `@density` with the standard metadata;
  `prob = @density` is a live binding (composer-world ARG_CHAN). No
  param-to-param wiring between stages in v2 (no ARG_NODE analog) — events
  are the only inter-stage traffic.
- `chain <name> { input midi?; stage <n> <cat>::<plugin> {…}; sink {…}+ }`.
  Textual order IS execution order — hence the keyword `stage`, not `node`
  (in `.dsp` order is irrelevant and edges carry topology; same word would
  teach editors the wrong intuition).
- Sinks: `{ channel = N; }` for notes, `{ channel = N; chanarg = "x"; }`
  for values; multiple sinks = fan-out; type filtering happens at the
  sink so one generator can drive melody and a filter sweep at once.
- `scale <name> "F3 Ab3 …";` — named pitch sets parsed once at load;
  NOTESET params accept a scale identifier or a quoted literal.
- Writer's rules are in the spec (the GUI will write these files):
  round-trip units as authored, write defaulted params explicitly, knob
  bindings persist as `@name`, never persist a seed the user didn't pin.

### 3d. Piano roll — `PianoRoll.h` / `PianoRoll.cpp`

- `Gtk::DrawingArea` in `src/gui/` style (`set_draw_func` → `onDraw`, GTK4
  event controllers, GPL header block).
- Now-line at 2/3 width; right of it, `peekPending()` drawn as outlines
  under a dimming wash — the actual scheduled future. Delivered notes
  solid, alpha from velocity, golden-angle hue per channel.
- Driven by `add_tick_callback` (frame clock), not a timeout; delivery
  handler only appends. **No mutex, on purpose** — same-thread producer
  and consumer; the header says so, so nobody "fixes" it.
- Pitch range auto-fits visible notes with easing (octave floor, lane of
  padding). History pruned to 4× visible span; scrub-back capped to match;
  scrubbing to live or double-click resumes follow; scroll wheel zooms.
- Bars end at note-off — release tails are envelope knowledge the
  scheduler doesn't have; render honestly.

## 4. Artifact inventory → where it lands

| sketch file       | destination (suggested)                             |
| ----------------- | --------------------------------------------------- |
| `thcomposer.h`    | `libthink/thcomposer.h` (pure interface, no deps)   |
| `eno_line.cpp`    | `plugins/composer/eno_line.cpp`                     |
| `thcScheduler.h/.cpp` | `src/thcScheduler.*` (app layer, beside MIDI plumbing) |
| `GEN_FORMAT.md`   | repo root, beside `DSP_FORMAT.md`                   |
| `airports2.gen`   | `gen/airports.gen` (new sibling of `dsp/`, `patches/`) |
| `PianoRoll.h/.cpp`| `src/gui/PianoRoll.*`                               |
| `genplay.py`, `airports.gen` (v1) | `scripts/` or drop; prototype only  |

Sketches compile against an idealized neighborhood — expect signature
reconciliation with the real `thSynth` (exact `addNote`/release/chanarg
call shapes), `thExport.h`, and CMake wiring for a new plugin directory.

## 5. Deliberately unwritten / to do

Everything this section originally listed is now written (see STATUS
above for where each landed and what changed on the way). What remains
open, in rough priority order:

- ~~Live MIDI into chains~~ — LANDED, all three halves at once.
  `THC_EV_NOTEOFF` joined the ABI (a NOTE with duration <= 0 is "held
  until further notice"; the scheduler tracks held notes and a pause
  releases them like everything else). ComposerWindow connects the
  m_sigNoteOn/Off hop into `injectMidiEvent`, so every chain with
  `input midi' hears the hardware keyboard; `gen::arp` is the
  receive-from-live plugin that forced the question, and it also
  arpeggiates composed chords from upstream, nothing configured
  differently. The stopped-transport question is decided: injected
  events falling out of chains on a paused clock deliver immediately
  — keys pressed while paused should sound. quantize snaps offs with
  the same snap as ons so a held release cannot miss its press. Still
  open in this area: the on-screen Keyboard widget does not emit
  m_sigNoteOn, so only hardware MIDI reaches chains from outside.
- `composer_input` and `composer_serialize`/`deserialize` — the two ABI
  additions §7 below argues for; neither blocks anything current.
- Chanarg strip in the piano roll normalizes 0–1; should use the arg's
  declared `.min/.max` once param metadata is reachable from the widget.
- A file chooser for *opening* a piece (New/Save/Save As exist; the
  window still opens on `gen/airports.gen` or `$THINK_GEN_PATH`).

The GUI-side `.gen` writer is no longer on this list: it landed as
**`thcGenEdit` + the Edit panel in ComposerWindow**, and it preserves
comments rather than regenerating files — NodeEdit's philosophy carried
over wholesale, after the whole-file-generation reading of §7 was
reconsidered. Each operation replaces the exact token span it is aimed
at, located through the loader's own tokenizer (`thcGenToken` carries
byte spans for precisely this). The editing model in the window is
NodeEditor's work-copy flow: edits splice a temp copy, Save publishes
it. Value edits (params, knobs, tempo) splice *and* poke the live
stores, so the piece keeps playing; structural edits (stages, chains,
sinks, scales, seed) splice and reload, which rewinds to zero — the
honest reading of the determinism story when the piece changed shape.
gencheck grew a fourth section that runs one of every operation against
a scratch copy of the shipped piece and then proves every comment
survived, no-op writes change no byte, guard rails hold (last sink,
duplicate names, unknown units), and the edited file still loads.

## 6. Suggested build order

1. `thcomposer.h` into libthink + `thcPlugin` loader + `eno_line` built as
   a shared object. Hardcode a chain in C++ — no parser yet.
2. `thcScheduler` against the real `thSynth` calls. **Milestone: sound.**
   A/B against `genplay.py` for sanity.
3. `PianoRoll` fed by the scheduler. **Milestone: picture.** These two
   milestones prove every interface before any grammar work.
4. `.gen` loader (lexer additions, grammar, loader validation, scales,
   knob binding). Milestone: `airports2.gen` plays.
5. Replay determinism test in ctest (reset → render → diff event streams).
6. `quantize` / `walk` / `humanize` / `euclid`; param panel binding for
   `@knobs`; tier-two `composer_draw` hosting in the editor.

## 7. How other algorithm families map onto the interface

The framework claims to be general; here is that claim checked against
the families worth having, and what each one would stress. The short
version: everything that composes by *deciding* fits today; what
composes by *learning or evolving* fits at run time but cannot yet save
what it learned, and what composes by *being judged* has no feedback
channel richer than a param. Both gaps have contained fixes, neither of
which needs to happen before milestone 4.

**Euclidean rhythms, random walks, arpeggiators, echo/humanize/quantize
(the planned launch set).** All landed. Euclid is a clocked generator;
walk drives chanarg sinks; the transformers are two-line `receive()`
bodies. The arpeggiator (`gen::arp`) turned out to be the load-bearing
one: needing to know what is held NOW is what forced THC_EV_NOTEOFF
into the ABI, and it holds both live keys (until their off) and
composed notes (for their duration), so the same stage breaks a
hardware chord and an eno_line's pads.

**L-systems.** LANDED as `gen::lsystem`: axiom and rules are
`THC_PARAM_STRING` params re-derived in `composer_param_changed` — the
hook was designed for exactly this — and the whole derived phrase is
emitted as one scheduled block, so the piano roll's ghosted right half
finally draws something (gencheck asserts it stays that way). Brackets
push and pop *time as well as degree*, so a branch runs in parallel
with what follows it: polyphony falls out of the grammar. No randomness
anywhere in the plugin. Exponential rules are capped, truncating the
tail rather than failing — for music, the right kind of wrong.

**Cellular automata.** LANDED as `gen::ca`: a ring of cells under one
of Wolfram's 256 rules, one row per tick, cells mapped onto the pitch
ladder. The rule is an ordinary param, so a knob re-threads the texture
mid-piece (gen/loom.gen puts Rule on a slider). `scatter' chooses the
single-center-cell start (no randomness at all) or a seeded random row;
`trigger' chooses births-only or every-live-cell. The `composer_draw`
is the scrolling grid, and it is the plugin's entire state made
visible.

**Markov chains.** LANDED as `gen::markov`, the plugin the dual entry
points were designed around: `receive()` trains (order 1 or 2),
`tick()` emits the weighted walk, and `pass' decides whether the
teacher sounds alongside the dream. Today the teacher is the upstream
stage -- put it after an lsystem and it studies the grammar while
paraphrasing it, which is loom.gen's whole trick and needed no MIDI
wiring. When live input lands, the same `receive()` trains on playing,
unchanged. The draw is the transition heat grid. Two things surfaced
in the original analysis and still hold:

- *Determinism has a boundary.* A replay is exact only if the inputs
  are; a model trained on live playing replays only the part after the
  training stopped changing. That is not a flaw — reset() recreating the
  instance wipes the table, which is the honest meaning of "replay the
  piece" for a learning composer — but the ctest replay gate should
  test learned composers only through scripted injectMidi streams.
- *Learned state does not round-trip.* Params are the only thing the
  .gen writer will persist, and a trained table is not a param. If a
  trained Markov voice should survive a session, the ABI wants an
  optional `composer_serialize`/`composer_deserialize` pair (an opaque
  blob the .gen file carries base64'd, or a sidecar file). Deliberately
  not designed further here; it is a v2 ABI addition with a version
  bump, not a v1 blocker.

**Genetic algorithms.** Three shapes, in rising order of friction:

1. *Autonomous GA* — fitness is a heuristic the plugin computes.
   LANDED as `gen::evolve`: phrase genomes over the pitch ladder,
   tournament selection, single-point crossover, per-gene mutation,
   two elites, and a fitness function documented as taste with a
   number on it (target density, leap tax, cadence on the root,
   monotony penalty). Each tick plays the current champion as a
   scheduled block and then runs one generation, so the piece *is*
   the search; the mutation knob reintroduces doubt mid-piece. Replay
   determinism is gated in gencheck — a GA drifting off its seed
   would be the least debuggable corruption of the story, so it is
   the one most worth a tripwire. `gen/growth.gen` is the demo: an
   lsystem canopy over an evolving bass.
2. *Interactive evolution* — the user is the fitness function. The only
   feedback channel today is a param ("rate the last phrase 0–5" as a
   FLOAT the panel exposes), which works but is clunky: params are
   continuous knobs, and selection is an event. A first-class fix is a
   small one: an optional `composer_input(state, event)` entry point,
   fed by the host from UI gestures (including clicks on the plugin's
   own `composer_draw` area, which is currently draw-only). Same
   pattern as param_changed: optional export, host checks for it.
3. *Fitness from the sound itself* — judging phenotype (audio) rather
   than genotype (notes). The probe/visual machinery already publishes
   per-window samples to the GUI thread, so the host *could* feed a
   composer probe data the way thVisual::feed works. That is a real
   seam but a speculative one; note it and walk past.

**Constraint solvers (species counterpoint, harmonization).** The
strongest argument for two decisions already made: events may be emitted
arbitrarily far ahead (a solved phrase is scheduled as a block), and the
piano roll draws the scheduled future — you watch the solver's committed
plan approach the now-line. A solver that needs to *revise* a plan is
the stress case: there is no "unemit". The contained fix, if ever
needed, is a host call to drop a chain's pending events beyond a time —
scheduler-side only, no ABI change. Until then: emit late rather than
early if revision matters. Related caveat for all clocked far-future
emitters: pending_ is keyed in seconds, so a tempo change after emission
does not re-map beat-domain plans; emitting a bar or two ahead keeps
tempo automation honest.

**Live coding / external control (OSC, scripts).** Not a plugin problem
at all: `injectMidi` generalizes to injecting any thcEvent into a
chain's head, and a text console driving param stores by name is already
possible through `thcParamStore::set(name, value)`. Mentioned so nobody
builds a plugin where a host feature belongs.

What this adds to §5's list, in priority order: nothing before milestone
4; then `composer_input` (unlocks interactive evolution and makes
tier-two visualizers into controls), then serialize/deserialize (unlocks
persistent learned state), then pending-revocation if a planner ever
wants it.

## 8. One language or two: reconciling .gen with .dsp

The question was bound to come up: the formats deliberately share a
lexical layer, live side by side, and are read by two parsers. Should
they be one thinklang parser? Checked against what actually divides
them, the honest answer is: unify the *lexer* soon, the *grammar* only
when a language-level feature pays for it, and treat one hidden
semantic difference first because it blocks everything else.

**What genuinely divides them today:**

- **`ms` means two things.** `.dsp` folds `5 ms` into *samples* inside
  the grammar action (times TH_SAMPLE at parse time); `.gen` keeps
  seconds and defers `beats` to read time. One token, two meanings, and
  the fold bakes the sample rate into every parsed `.dsp`. Any shared
  grammar would have to carry both meanings keyed by dialect — or,
  better, `.dsp` stops folding in the grammar and hands `(value, unit)`
  to buildArgMap to fold. That change is independently right (it makes
  `.dsp` sample-rate honest) and is exactly the kind the dspcheck
  corpus sweep exists to gate.
- **thinklang is not reentrant.** Globals (`parsetree`, `parsenode`, a
  static synth) and build-during-parse actions that construct the
  thSynthTree directly. The `.gen` loader is reentrant and builds
  through a passed scheduler.
- **Layering.** libthink cannot see thcPlugin or thcScheduler, so a
  unified parser's `.gen` half must produce a neutral document that
  src/ walks — which is a shape the `.dsp` half does not have yet.
- **Order semantics** differ per construct (irrelevant in `.dsp`,
  load-bearing in a chain), but that is grammar-local and easy.

**The staged recommendation:**

1. *Make thinklang pure* (reentrant, no globals), behavior identical,
   corpus-gated. Worth doing regardless of any merge.
2. *Unify the lexer, not the parser.* Teach the flex lexer to emit
   tokens with byte offsets and let it feed both consumers: the bison
   grammar for `.dsp`, and thcGenLoader's recursive descent for `.gen`
   (which already consumes a token vector and is better at name-and-
   line errors than yacc will ever be). "Same lexical layer by
   construction" kills the drift risk, which is most of what the
   two-parser smell actually is. Bonus: offsets in the shared lexer are
   what would let NodeEdit adopt thcGenEdit's span-splicing and retire
   its line-based scanning.
3. *Merge grammars only for a language payoff.* The genuinely exciting
   convergence is not parser hygiene but the language where a piece
   can carry its instruments — `.gen` chains beside inline `patch`/
   `node` blocks, sinks binding to named patches instead of channel
   numbers, one self-contained shareable file. If that feature gets
   scheduled, grammar unification is its natural first commit. Without
   it, a merged grammar is churn in the most load-bearing code in the
   tree.

Step 0 for all of it is the `ms` fold, because a shared lexer that
hands `.dsp` a folded sample count and `.gen` a second is not shared —
it is two lexers wearing one coat.

## 9. Style notes for new code

Match the house: GPL header block on every file, `onX` handlers /
`PascalCase` public mutators as in `Keyboard.h`, trailing-underscore
members, prose-comment style that records *why* (see the existing docs —
decisions get argued in comments, not just stated). Written deliverables
should read plain and human, not AI-flavored.
