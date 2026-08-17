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
>   the shared grammar folds `ms` into *samples* (engine semantics a
>   .gen value must not inherit) and cannot see thcPlugin/thcScheduler
>   from libthink. GEN_FORMAT.md is implemented as written, including
>   "a file with any error loads nothing" and name-and-line messages.
>   The *lexical* layer began as the .dsp one reproduced faithfully;
>   §8 step 2 has since replaced the copy with the thing itself
>   (`libthink/thLexer.h`), so the grammars are still two and the
>   scanner is one.
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
> - **`.gen` channels are 1-16**, not the wire's 0-15. The main window's
>   patch tabs and the Keyboard window's spinner have always counted
>   from one and so does every sequencer, and a file that disagreed with
>   the thing next to it on screen is a confusion nobody needed. The
>   loader converts at the file boundary, the way note names are
>   resolved there; `channel = 0` is a load error naming the change,
>   which is the one spelling that tells a file written for the old
>   numbering apart from one written for this.
> - **`gen/README.md` indexes the shipped pieces**, ten of them, each
>   built around one idea and gated by gencheck's corpus sweep: every
>   piece must load, and every piece with a generator in it must deliver
>   something inside a minute. A piece that loads and then says nothing
>   is a piece with a typo in it.

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

- **`ms` means two things.** `.dsp` folds `5 ms` into *samples*; `.gen`
  keeps seconds and defers `beats` to read time. One token, two
  meanings, and the `.dsp` fold used to happen in the grammar action at
  the compile-time sample rate. Half of this is settled: `.dsp` now
  hands `(value, unit)` out of the grammar and folds at load, which was
  independently right and is what the dspcheck corpus sweep existed to
  gate. What remains divided is only that the two languages mean
  different things by a duration, which is a fact about them rather
  than a defect in either — and which a shared *grammar* would have to
  carry keyed by dialect.
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

1. *Make thinklang pure* — DONE (branch thinklang-pure): api.pure
   bison threading a thParseContext, reentrant flex with yylineno and a
   `think' prefix, the whole loading ritual folded into thParseDsp, and
   the stale tracked generated sources deleted (the source-dir copy of
   thinklang.h could shadow the fresh build-dir one, which is a bug
   that was waiting). parseTree runs mutex-free now; the corpus sweeps
   prove behavior identical, and a hundred concurrent parses prove the
   purity is real.
2. *Unify the lexer, not the parser* — DONE (branch thinklang-lexer).
   `libthink/thLexer.h` is the lexical layer on its own: `thLexToken`
   carries kind, text, value, line and byte span, and `thLexString`
   hands back an END-terminated vector. thinklex.ll produces it, and
   both consumers read it — thinklang.yy's `yylex` maps tokens onto
   grammar codes, and `thcGenLoader::tokenize` adapts them to .gen's
   shape. .gen's hand-written copy of the .dsp rules is deleted.

   The division that made this work: the shared layer is *lexical* and
   nothing more, and vocabulary belongs to each language. Every
   identifier comes out a WORD; `ms` is a keyword in thinklang.yy's
   shim and an ordinary unit name to .gen, `beats` the reverse. A
   lexer with a dialect switch would have been two lexers wearing one
   coat, which is the thing step 0 warns about.

   .gen's adapter fuses two pairs the shared lexer keeps apart — `-`
   onto the number after it, `@` onto the name after it — because .gen
   has neither arithmetic nor an `@` operator to hang them on.
   Adjacency is checked by span, so `-5` is a literal and `- 5` is the
   error it always was. Punctuation .gen has no use for is refused with
   the wording the old scanner used.

   Two behavior changes worth knowing. A stray character used to hit
   flex's default rule, which echoed it to stdout and carried on; it is
   an ERROR token now, and `thParseDsp` refuses the file before a
   grammar action has built anything. And `.dsp` is lexed from a string
   rather than pulled through a FILE*, because offsets into the text as
   it sits on disk are the point — which is what NodeEdit needs to
   adopt thcGenEdit's span-splicing and retire its line-based scanning.

   `scripts/lexcheck` is the gate: spans cut the source back into the
   tokens that claim them, lines survive comments and blank lines,
   refusals name what and where, .gen's fused tokens still begin where
   the shared lexer began one, and sixteen concurrent lexes match
   sixteen lone ones. It earned itself immediately — `yy_scan_bytes`
   builds its buffer without going through `yy_init_buffer`, so
   `yylineno` started at whatever the malloc'd block held and every
   error named a line from the previous parse.

   Step 0 (the `ms` fold) turned out not to block this after all: the
   fold lives in a grammar action, not in the lexer, so the shared
   scanner already hands both languages an unfolded `(5, "ms")`. It
   remains worth doing on its own merits — it is what makes `.dsp`
   sample-rate honest — but it is no longer the runway for anything.
3. *Merge grammars only for a language payoff.* The genuinely exciting
   convergence is not parser hygiene but the language where a piece
   can carry its instruments — `.gen` chains beside inline `patch`/
   `node` blocks, sinks binding to named patches instead of channel
   numbers, one self-contained shareable file. If that feature gets
   scheduled, grammar unification is its natural first commit. Without
   it, a merged grammar is churn in the most load-bearing code in the
   tree.

Step 0 for all of it was thought to be the `ms` fold, because a shared
lexer that hands `.dsp` a folded sample count and `.gen` a second is
not shared — it is two lexers wearing one coat. Doing step 2 showed
the fold was never in the lexer: it is a grammar action, and the
scanner hands out `5` and `ms` as two tokens to whoever asks. So the
fold was independent work rather than a prerequisite — and it is now
DONE too (branch dsp-unit-fold), for its own reason.

That reason turned out to be worse than "sample-rate honest" implied.
The grammar folded with the compile-time `TH_SAMPLE`, so `thinksynth
-r 48000` opened the device at 48k and then played every envelope in
every patch 8.8% short: the durations had been converted for a rate
nothing was running at. The grammar now records `(value, unit)` and
`thSynthTree::foldUnits` converts once, in `finishParse`, at the rate
the synth was built with. One record per *value site* rather than per
arg, because `@decay = 500 ms` and `@decay.max = 88200` is the same
control with one site in milliseconds and one already in samples.
`libthink/thUnits.h` is the one copy of the arithmetic — the panel and
the `.dsp` writer unfold through it at the same rate, which they have
to, or a `-r` session would rewrite files to mean something else. Node
args carry their unit now as well as chanargs. A unit inside
arithmetic (`5 ms + 3`) is a parse error rather than the accident it
used to produce. `argtype` gates it: a duration follows the rate, a
percentage ignores it, a range written without a unit is left alone,
and the corpus sweeps prove 44100 is byte-identical to before.

## 9. When the algorithms reach the instruments

The question behind §8's "language payoff" deserves its own section:
what if the algorithmic processes controlled not just the notes but the
instruments — their timbre, their evolution, and the piece's structure?
Checked against the architecture, the surprising answer is that most of
the machinery already exists, some of it built for other reasons. What
is missing is language and policy, not plumbing.

**Three tiers, in rising depth of reach.**

*Tier 1 — playing the declared surface.* This exists. Chanarg sinks let
any composer drive whatever `@args` a patch declares, scheduled and
replay-deterministic like everything else. A generative process already
shapes timbre — but only along axes the instrument chose to expose.

*Tier 2 — evolving the surface.* LANDED (branch composer-presets). A
patch's declared chanargs form a vector of floats, and a vector of floats
is a genome. The noun arrived first, because everything else was blocked
on it: `preset <name> { arg = value; … };` is a named chanarg vector the
piece file carries, and `THC_PARAM_PRESET` delivers it to a plugin
resolved — `"res=0.86,fmin=0.04"` — on exactly the terms
`THC_PARAM_NOTESET` delivers pitches, so no composer ever looks a preset
up. Additive enum value; interface version stays 1.

`gen::morph` is the first thing to use it: two presets, the line between
them, emitted as scheduled `THC_EV_CHANARG` events. It exports both entry
points and they are genuinely different pieces of music — `gen::morph`
sweeps on its own clock under whatever is playing, while `xform::morph`
does not tick at all and instead schedules a whole sweep from the time of
each note passing through, so the instrument opens as it is played and
the roll's ghosted half shows the sweep coming. No randomness anywhere,
which makes its replay gate a sharper tripwire than a seeded composer's:
a divergence there is the scheduler, not the plugin.

Two things the sketch above did not anticipate:

- **The sink had to grow a wildcard.** "No host change at all" was true
  of the ABI and false of the delivery path. A named chanarg sink
  *overwrites* the name on every event — correct for a walk, which
  produces a number and should not know which knob it lands on — and a
  vector routed through one arrives as a single knob taking each
  component's value in turn. `sink { channel = 2; chanarg = "*"; }` keeps
  the name each event carries. Scheduler-side only; `*` cannot collide
  with a real name because a chanarg is a `.dsp` identifier.
- **Presets take no quoted literal**, unlike note sets. A one-off pitch
  pool written inline is reasonable; a one-off timbre vector spelled as
  text is a preset that cannot be morphed towards, bred from or saved
  under a name, which is the entire reason the noun exists. The loader
  says so rather than accepting it.

A component named by one preset and not the other holds still at the
value it was given, so a target can be a correction rather than a
restatement. `gen/tide.gen` is the demo: `dusk` and `noon` forty seconds
apart on channel 2, and a struck bell that opens itself through
`xform::morph`. gencheck gates the resolution, the eight rejections, the
wildcard routing, the exact endpoints and replay; it also grew a corpus
sweep, because until now nothing loaded the shipped pieces other than
the one it was handed.

The second half of the tier is `gen::breed`, and it is `gen::evolve`
pointed at a vector of knobs instead of a vector of degrees: a
population, tournament selection, single-point crossover, per-gene
mutation, elites carried unchanged, and the champion *played* every
cycle. The fitness is taste with a number on it and says so — `aim`
pulls toward a target preset, `drift` rewards being unlike the timbre
just played (evolve calls the same term `boredom`, for the same reason:
without it a GA finds a local optimum in a minute and holds it forever),
`reach` mildly rewards using the corridor rather than huddling at one
end.

The one design question that did not carry over from `evolve` is where
a timbre genome's bounds come from. A phrase's genes are degrees on a
ladder the piece hands over; a timbre's genes are *knobs on somebody's
instrument*, and a search free to drive them anywhere would be reaching
past what the patch declared. So the corridor is the piece's own
presets: each component travels between the values `from` and `toward`
give it, widened by `spread`, and a component neither preset mentions
cannot be invented. That is §9's first principle — the declared surface
is consent — arriving as arithmetic rather than as a rule someone has to
remember, and gencheck asserts it directly by checking that no emitted
value leaves the interval and no unnamed component appears.

Audio-feature fitness is still staging step 5's shadow synth, and
deliberately so. What `breed` needs to become that is a different
`fitness()` and nothing else, which is most of the argument for having
built it this way first.

`thcGenEdit` reads and writes presets now: `addPreset`,
`setPresetValue`, `addPresetValue`, `removePresetValue`, `removePreset`,
spliced by span like everything else, with the guard rails the format
implies — a preset that sets nothing does not load, so no operation may
leave one, and a preset something still names is refused rather than
removed. That last one is where a preset differs from a scale: a scale's
references are inlined as its literal note list on the way out, and a
chanarg vector has no literal form to inline, on purpose. So the choice
was refuse or silently break the file. The Edit panel draws a spin
button per component, and moving one is a *value* edit — spliced and
poked into every live stage naming that preset — so a component can be
dragged while a morph is sweeping through it.

Tier 2 is closed. `gen/tide.gen` travels between two presets;
`gen/bloom.gen` searches for one.

*Tier 3 — the graph as genome.* The instrument's synthesis topology
itself becomes the evolving material. This sounds far-fetched until the
inventory is taken:

- `loadTree(filename, channum, amp)` is a *live atomic instrument
  swap*: the replacement is built fully off the audio thread, a
  `SET_CHANNEL` command queues the exchange at a window boundary, and
  the retire queue hands the old channel back for safe deletion. Hot
  instrument replacement is not a feature to build — it is the normal
  patch-load path, running since before this project started.
- thinklang is pure (§8 step 1), so candidate instruments parse
  anywhere, even concurrently — a background evaluator can parse a
  population without touching the live synth's state.
- NodeEdit and thcGenEdit prove the validated-splice model: a program
  can edit instrument text through the same one-writer seam a human
  uses, comments preserved, atomicity guaranteed.
- `think_nodemodel` (NodeGraph + NodeCatalog, deliberately GTK-free) is
  the vocabulary of *legal* graphs — `canConnect` is exactly the
  constraint function a graph-mutation operator needs so that offspring
  are well-formed by construction rather than by luck.
- The headless harnesses (dspcheck, dsplevel, dspab) render patches
  in-process with no audio device. That is a fitness lab: a second,
  *shadow* thSynth renders candidates offline while the live one plays,
  and scores them on audio features (RMS, envelope shape, spectral
  measures — kissfft, per the visualizers' verdict on the fft/ relic).
  Judging phenotype instead of genotype, which §7 filed as speculative
  for note-fitness, is for timbre-fitness the whole point.

**The ABI shape: intents out, services in.** Composers deliberately
cannot link libthink, and that stays true. A plugin never touches a
graph; it emits *intents* the host executes — the same relationship
sinks already express. Concretely: a `THC_EV_PATCH` event ("channel n
becomes patch p at time t") makes instrument changes schedulable,
which means the piano roll draws them (a program-change lane) and
replay determinism covers them. Tier-3 evolution runs host-side as a
service a `meta::` plugin steers, not as plugin code holding graph
pointers.

**Structure, two roads.** *Structure as data*: the piece file grows
`section` blocks (which chains run, which patches bind, for how long)
and a meta chain schedules or chooses among them — deterministic,
drawable, savable, and the piano roll's future-window shows the form
approaching. *Structure as rewriting*: meta-composers splice the piece
itself through thcGenEdit, so the piece is self-modifying and Save
publishes what it became — the "same file, same seed" story becomes
"same file, same seed, same final file", which still verifies but reads
stranger. Data first; rewriting is the research branch.

**Why this lands on §8.** Tiers 2 and 3 all want the self-contained
file: inline `patch` blocks, sinks binding patch *names* instead of
channel numbers, presets as named vectors, sections as form. That is
precisely the "language payoff" §8 step 3 said grammar unification
should wait for. This section is that payoff arriving; the `ms` fold
and the shared lexer stop being hygiene and become the runway.

**Staging, each step shippable alone:**

1. The `ms` fold and the shared offset-carrying lexer (§8 steps 0 and
   2) — DONE, both halves. `libthink/thLexer.h` feeds both languages
   and carries byte spans, so `patch` blocks inline in a `.gen` would
   be read by the same scanner that reads them in a `.dsp`, by
   construction rather than by care; and a `.dsp` value no longer
   arrives pre-converted at a rate nobody chose, which is what step 2
   below would otherwise have inherited the moment a piece carried its
   instruments.
2. `patch` blocks inline in `.gen`; sinks bind by patch name; the
   loader instantiates channels. One shareable file that carries its
   instruments.
3. Presets in the language; a `gen::morph` transformer (writable even
   before this, better after) — DONE, together with the wildcard
   chanarg sink the sketch had not noticed was needed, the editor
   operations presets turned out to need, and `gen::breed`, the GA over
   chanarg vectors. Tier 2 above is closed; what is left of it is the
   audio-feature fitness at step 5.
4. `THC_EV_PATCH` + scheduler service via the `SET_CHANNEL` path;
   program-change lane on the roll.
5. Shadow-synth fitness service + `composer_input` (§7's pending item).
   The chanarg-genome GA itself is done (`gen::breed`, tier 2); what
   this step adds is judging the *sound* rather than the vector, which
   for `breed` is one function replaced and nothing else moved.
6. Sections and meta chains; then, if the appetite is real, graph
   mutation constrained by nodemodel.

**Three principles to hold while building it.** The declared surface is
*consent* — an instrument states how it may be played, and deeper reach
is a language change the patch opts into, never a backdoor. Algorithms
use the human seams — thcGenEdit, NodeEdit, loadTree — so there is one
writer and no second path that can corrupt a file. And everything
schedulable is drawable: if an instrument change is an event, the roll
shows it coming and gencheck replays it byte-identically, or it does
not ship.

## 10. Style notes for new code

Match the house: GPL header block on every file, `onX` handlers /
`PascalCase` public mutators as in `Keyboard.h`, trailing-underscore
members, prose-comment style that records *why* (see the existing docs —
decisions get argued in comments, not just stated). Written deliverables
should read plain and human, not AI-flavored.
