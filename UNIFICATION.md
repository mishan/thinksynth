# Uniting gen and dsp — a plan

The end state: one language in which a piece carries its instruments,
composers can reshape those instruments while they play, and DSP nodes can
participate in the composition process itself. This is the SuperCollider
move — Csound split orchestra from score, SC folded them back into one
language, and `.dsp`/`.gen` is precisely an orchestra/score split walking
the same road.

## 0. The distinction that keeps this sane

Unify the **language**, not the **semantics**. The two domains differ in
ways that are load-bearing, not incidental:

|                | dsp graph                    | composer chain              |
| -------------- | ---------------------------- | --------------------------- |
| rate           | audio, per window            | events, sparse              |
| topology       | edges; statement order moot  | textual order IS the order  |
| instances      | copied per sounding voice    | singleton per placement     |
| thread         | audio callback               | GUI thread                  |
| keyword        | `node`                       | `stage`                     |

Every phase below merges files, namespaces, and bindings — never these
columns. The unified language keeps both keywords on purpose: the day a
`node` can appear where a `stage` goes with no marker, editors inherit
wrong intuitions about order, lifetime, and thread, and the debugging
sessions write themselves. Audio never flows through a chain; events
never flow through a patch. What crosses the boundary is *values* (knobs,
chanargs) downward and *structure edits* (phase 4) downward. Nothing
crosses upward into the audio thread except through the command queue,
same as ever.

## Phase 1 — one file: pieces carry their instruments — LANDED

The smallest real unification, and independently the answer to "a .gen
isn't self-contained."

> **DONE.** `instrument pad { dsp "amb01.dsp"; a = 900 ms; };` is in the
> grammar, `sink { instrument = pad; }` is the primary binding,
> `gen/airports.gen` opens and plays with nothing loaded first, and
> `gencheck` grew a section for all of it. GEN_FORMAT.md §4b is the
> spec. Deltas from the sketch below, in the order they surfaced:
>
> - **Channel allocation is deferred to the end of the parse**, not done
>   as each instrument is read. It cannot be done any earlier: a
>   `channel = N` sink further down the file is a claim on a number, and
>   an instrument allocated before that claim was read would have to
>   either collide with it or refuse a file that mixes the two spellings.
>   So instruments go in without channels, sinks that named one are
>   parked, and one pass at the end hands out the lowest free number in
>   declaration order and fills the sinks. Which means both spellings
>   live in one file with neither having to come first.
> - **Instrument values carry units, and the unit is checked against the
>   chanarg's own declaration.** `a = 900 ms` folds through `thUnits` at
>   the rate the synth was built with — the same arithmetic §8's
>   `dsp-unit-fold` put in one place. A duration written bare is refused
>   and so is a unit on an arg that has none, for the reason
>   GEN_FORMAT.md §2 refuses a bare duration on a stage param: the unit
>   decides what the number is, so its absence decides nothing. This is
>   the one place `%` reaches `.gen`, since `aspect2.dsp` declares
>   chanargs in it; everywhere else in a piece it is still a stray
>   character.
> - **The load itself is a host hook.** `thcScheduler` owns the table and
>   applies the values; *loading a graph onto a channel* goes through a
>   `std::function` the app fills in with `gthPatchManager::newPatch`,
>   because in the app an instrument is also a patch tab, a filename and
>   an arg panel, none of which the composer host can see. The default
>   with no hook installed is plain `loadTree`, which is what makes the
>   path gateable headlessly rather than only reachable through the GUI.
> - **An arg the graph does not declare is refused**, where a `.patch`
>   would invent it. Patches predate arg metadata and half the corpus
>   sets things no graph declares; a piece file has no such history, and
>   §9's "the declared surface is consent" reads more naturally as a rule
>   the loader enforces than as one an author remembers. The same check
>   reaches a *sink's* chanarg when the sink names an instrument, since
>   then the graph is known — a knob the instrument does not have would
>   otherwise deliver into `getChanArg`'s NULL forever, in silence, a
>   long way from the typo. A `channel = N` sink is exempt: its patch is
>   somebody else's and may not even be loaded yet.
> - **A file with any error loads nothing, channels included.** That
>   sentence was true of the chains from the first version of this loader
>   and briefly false of the channels: an instrument that came up before
>   a later one failed stayed up, silent, on a tab, for a piece nobody
>   was going to hear. So a failed load takes back every instrument it
>   had applied, through an unload hook that is the mirror of the load
>   one and defaults to `removeChan` the same way.
> - **The window gives channels back, and does not take ones that are
>   not free.** `ComposerWindow` remembers which channels it filled for
>   the piece that is open, so a piece that drops an instrument does not
>   leave a patch tab behind that nothing plays. The same list answers
>   the other half: a second hook, `channelTaken`, tells the loader which
>   channels already hold somebody else's patch, so opening the Composer
>   allocates around a patch you loaded by hand instead of replacing it —
>   while the channels the *previous* load of this piece was on stay
>   reusable, or an instrument would walk one to the right on every
>   reload. And an instrument whose *whole declaration* is unchanged is
>   left alone rather than rebuilt, because every structural edit
>   reparses the piece and rebuilding meant renaming a knob's label cut
>   every sounding voice on the pad. The whole declaration and not just
>   the `.dsp`, because applying an instrument only writes the values its
>   block lists: one that kept its graph and *dropped* a line would keep
>   the value that line used to set. A channel is likewise only given
>   back if what is on it is still the graph this window put there —
>   somebody who loaded their own patch onto one of the piece's channels
>   has made it theirs.
> - **`thcGenEdit` learned the block by reading and the sinks by
>   writing**: `describe` reports instruments with their authored
>   right-hand sides, and `addSink`/`setSink`/`addChain` take a target
>   that is a name or a number. Switching a sink between the two replaces
>   the statement rather than editing inside it, because there is no
>   sense in which `channel = 4` can be edited into `instrument = pad`.
>   `addChain` taking a target is not symmetry: allocation works around
>   claimed numbers, so a new chain written `channel = 1` into a piece
>   whose instrument sits there does not collide — it *moves the
>   instrument*, for a chain the person only wanted to hear. And a sink
>   may only name an instrument declared *above* its chain, because the
>   loader resolves names in file order: checking merely that the name
>   exists somewhere was enough to write a file that would not load back,
>   in any piece that puts its chains above its instruments.
> - Authoring an instrument block from the panel is deliberately not
>   here. The noun landed before its panel, the way a preset's did.
>
> - **One bad instrument stops the rest.** The file is not going to load
>   once the first one fails, and every instrument after it would be
>   another graph put on another channel for a piece nobody is going to
>   hear.
>
> Three things found on the way that were nothing to do with instruments.
> `thcGenLoader::load` never cleared `presets_`, so a loader reused
> across files carried the previous piece's presets into the next one.
> `gencheck` had been filling the synth's command ring and printing
> "command queue full" for most of its run, harmlessly while the ring
> held only notes nobody listens to — and not at all harmlessly the
> moment loading an instrument started queueing a `SET_CHANNEL`; it
> drains once per render now. And `gthPatchManager::newPatch` deleted the
> channel's `PatchFile` *before* attempting the load, so a DSP that
> failed to parse left the old graph playing with nothing describing it:
> no filename, no tab contents, nothing able to unload it. `loadTree`
> does not touch the channel unless it succeeds, so neither does
> `newPatch` now. That one predates all of this and belongs to the Patch
> Selector as much as to here; loading an instrument on every piece load
> is just what made it easy to reach.

- New top-level block in the gen grammar:

```
instrument pad {
    dsp "string.dsp";            # graph by reference, or inline later
    cutoff = 0.4;                # chanarg presets, .patch semantics
};

chain loop_ab3 {
    stage src gen::eno_line { ... };
    sink { instrument = pad; };              # was: channel = 3
};
```

- `sink { instrument = X; }` replaces channel numbers as the primary
  binding; the loader allocates channels behind it. `channel = N`
  survives as the escape hatch for driving an externally-loaded patch.
- Inline `instrument pad { node osc osc::sine {...}; ... }` — the full
  dsp statement language inside the block — can come in a second step;
  the grammar work is importing the `.dsp` rules under one root, which
  the shared lexer was built to allow. By-reference alone delivers the
  self-contained piece.
- Loader: `thcGenLoader` grows an instrument table; on load it does what
  the patch selector does per entry. `describe`/`thvalidate --dump`
  learn the block; the MCP `render` tool's `patches:` argument becomes
  optional the moment pieces declare their own.

Deliverable: `airports.gen` with zero setup — open, press play. Done.

## Phase 2 — one binding namespace

Both domains already have ARG_CHAN semantics and the same metadata
system; today they are parallel universes. Merge them:

- A piece `@knob` may bind a stage param (already true) **and** an
  instrument chanarg (`cutoff = @warmth;` inside an instrument block).
  One knob, both worlds — the panel already knows how to draw it.
- `sink { instrument = pad; chanarg = "cutoff"; }` follows from phase 1
  automatically.
- Decision to make here, deliberately conservative: **chanargs remain
  the full extent of a composer's reach into an instrument.** The
  GEN_FORMAT line stating that limit stays true through phase 3. An
  instrument's mutation surface is what it declares — encapsulation, not
  a missing feature. Phase 4 revisits this with structure edits, which
  are a different mechanism, not a widening of this one.

Deliverable: one `@warmth` knob sweeping a filter and a density together;
a walk generator driving an instrument it named, not a channel number.

## Phase 3 — embedded nodes: dsp plugins as chain stages

"Nodes in the composition process": the same dlopen'd DSP plugins,
runnable at control rate inside a chain.

```
chain drift {
    stage lfo dsp::sine { freq = 0.05 hz; };
    stage src gen::walk { step = @lfo_out?  ... };
};
```

- Mechanism: a **control-rate host** on the GUI thread. When the
  scheduler evaluates a chain, embedded nodes are stepped against
  *transport time* with a tiny buffer (one sample per evaluation is the
  degenerate, correct case). Same plugin binaries, second interpreter,
  never the audio path — the table in §0 is why this is a new host and
  not a shortcut into `thSynth`.
- This is where the composer-world **ARG_NODE finally lands**, and the
  v2 spec's refusal now pays off: params gained node-binding only once
  there were nodes with defined evaluation semantics to bind, instead of
  a wiring syntax that meant nothing. A stage param bound to an embedded
  node's output is evaluated at the moment the stage reads it.
- Determinism rules extend, not bend: embedded nodes step on transport
  time (pause freezes them, replay replays them); any stochastic node
  draws from the piece seed. A noise node in a chain must be as
  replayable as a markov stage.
- Practical scope check before committing syntax: audit which plugin
  categories are meaningful at control rate (`osc`, `env`, `math`,
  `logic`, `filt` on control signals — yes; `fft`, `delay` in samples —
  no). The host should refuse the meaningless ones by category, loudly.

Deliverable: an LFO breathing a chain's density; an envelope shaping a
piece's dynamics over minutes — modulation *of the composition*, with
the same modules that modulate sound.

## Phase 4 — structural mutation: composers reshape instruments

The endgame, and the reason phases 1–3 are ordered this way: mutation
needs pieces that own their instruments (1), a namespace to address them
(2), and determinism discipline already extended once (3).

- New event class beside NOTE and CHANARG: a **structure edit** —
  add/remove/rewire a node, change a non-chanarg constant, swap a
  sub-graph. Delivered like everything else, but its `deliver()` path is
  the node editor's rebuild-and-swap: build the new voice tree on the
  GUI thread, swap through the command queue. The voice-lifecycle
  question is answered by whatever the editor already promises today —
  sounding voices finish on the tree they started with, new notes get
  the new tree — stated in the spec, not re-decided here.
- Rate-limit by design: structure edits are events, so they are sparse,
  scheduled, and visible on the piano roll like everything else. A
  composer cannot thrash the graph faster than the event stream flows.
- Then the genetic convergence: `breed`/`evolve`/`morph` operating on
  *instruments*. The genome is the graph description — which
  `dspwrite`-style serialization and `thvalidate` make printable and
  checkable — and the **fitness function is the MCP's render pipeline**:
  offline render, `analysis`-family metrics, select, breed again. The
  tuning loop and the evolution loop turn out to be the same loop run
  at different temperatures. Evolved timbres for evolved melodies, all
  replayable from a seed.

## Hazards worth writing down before they cost a branch

- **Grammar merge mechanics.** One root, two statement families. The
  shared lexer carries it; the bison work is real but mechanical. The
  non-mechanical part is diagnostics — an `=` where an `->` belongs must
  say "node wiring uses ->" and vice versa, or the unified language
  costs more confusion than the split ever did.
- **The second interpreter drifts.** Control-rate and audio-rate hosts
  running the same plugin must agree on semantics per sample. A
  `hostcheck`-style gate — same plugin, same input, both hosts, diff —
  belongs in ctest from phase 3 day one.
- **Mutation vs the panel.** A structure edit can invalidate chanarg
  metadata the panel is showing. The rebuild path already re-announces
  args on patch load; mutation must go through that announcement, not
  around it.
- **Format churn.** Phases 1 and 2 change what a `.gen` can say; the
  writer's rules (round-trip as authored) apply from the first grammar
  patch, or the GUI's save path and the new blocks diverge immediately.

## Order of work

1 → 2 are small and independently shippable; each makes the MCP better
for free. 3 is the first structurally new machinery (the control-rate
host) and where the determinism gates earn their keep. 4 rides on all of
it and should start life as one hardcoded structure edit end-to-end —
prove rebuild-and-swap under the scheduler before any composer is
allowed to breed a filter.
