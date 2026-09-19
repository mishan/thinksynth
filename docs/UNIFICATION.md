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
>   one and defaults to `removeChan` the same way. The one case where it
>   cannot keep that promise is a command ring too full to carry the
>   removal — the audio thread is wedged, or nothing is draining it —
>   and then the graph is still sounding while the piece that asked for
>   it is being thrown away. The scheduler keeps the instrument rather
>   than the record dying with the table, retries on its own clock, and
>   the loader says so among the errors instead of claiming the file
>   loaded nothing. Headless is where that matters most: the application
>   has a window keeping a second copy, and nothing else does.
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

## Phase 2 — one binding namespace — LANDED

Both domains already have ARG_CHAN semantics and the same metadata
system; today they are parallel universes. Merge them:

> **DONE.** `fmin = @warmth;` inside an instrument block, and the same
> `@warmth` on a stage param. `gen/weather.gen` is the deliverable: it
> carries its pad now, `Breadth` drives the top line's density and the
> pad's two mix chanargs from one slider, and `Tail` sets the pad's
> release and nothing else. GEN_FORMAT.md §4b is the spec. Three things
> worth knowing:
>
> - **The binding is a push, not a read, and the asymmetry is not a
>   shortcut.** A stage param bound to a knob is read through it, because
>   a composer asks its param store for a value whenever it wants one. A
>   chanarg cannot be read that way: what reads a chanarg is the audio
>   graph, and the only value it will ever see is the one sitting in its
>   `thArg`. So the knob's changed signal sets the arg, and the knob's
>   current value is pushed at load as well, or a piece would not sound
>   like its file until somebody touched a slider. The connection looks
>   the chanarg up by name every time rather than capturing the `thArg`
>   the load already had in hand — a channel can be replaced from under a
>   binding, the Patch Selector will do it on request, and the args go
>   with the `thMidiChan` that owned them.
> - **A knob binding carries a unit, exactly as a literal does.** `r =
>   @tail ms`, and a bare binding on a folded chanarg is refused the way
>   a bare literal is. The number in a knob is as unitless as the number
>   in a file; without this, an envelope on a slider would run in samples
>   and weather.gen's `Tail` would sweep four milliseconds to a hundred
>   instead of a fifth of a second to five. Phase 1's rule did not need
>   bending, only applying. The push re-checks the fold against whatever
>   arg it lands on, too — the re-lookup that stops it writing through a
>   freed pointer does not stop it writing into a *different* arg of the
>   same name, and `r` folded from ms on one graph beside `r` running 0
>   to 1 on the next is a knob nudge writing 88200 into an arg whose top
>   is 1. A binding whose target changed shape stops driving rather than
>   driving wrongly.
> - **A knob and a named chanarg sink may not share an arg.** Both are
>   pushes, so both writing `fmin` is last-writer-wins — the walk wins
>   every time it fires and the slider looks dead a second after you let
>   go. Nothing about that is visible from either end and no reading of
>   the file makes it deliberate, so the loader refuses it and names the
>   knob, and so does the editor before it writes one. This is why
>   weather.gen's `Breadth` reaches the mix rather than the filter: the
>   filter there belongs to the walks, and "belongs to" now means
>   something that can be checked. `chanarg = "*"` is outside it, for the
>   same reason it is outside the arg-exists check — the targets are in
>   the events — and that is the one way left to write the fight.
>
> The conservative decision below was kept, and is worth restating
> because this phase is exactly where it would have been easy to lose: a
> knob binding widens *who* may drive a declared arg, not *what* may be
> driven. GEN_FORMAT.md §4a's sentence about the declared surface stands
> untouched.
>
> Not here: an authoring surface for instrument blocks, so the knob
> panel's "drives" list shows instrument bindings without an Unbind
> beside them — a row that says what is true beats a button that cannot
> do what it offers. And the canvas has no instrument node yet, so a
> knob that drives *only* an instrument draws no wire; the Selection tab
> is where it says so. An instrument node on the canvas is the natural
> next piece of UI work and is not on phase 3's path.

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
Done — as `Breadth` over a mix and a density rather than a filter and a
density, because the rule above put the filter out of reach of a knob in
that particular piece, which is the deliverable arriving with an argument
attached rather than without one.

## Phase 3 — embedded nodes: dsp plugins as chain stages — LANDED

> **DONE.** `stage lfo osc::simple { freq = 0.05; }` inside a chain, and
> `prob = mid->out` on the composer stage that reads it.
> `gen/breath.gen` is the deliverable: an LFO breathing a chain's
> density on a twenty-second cycle, and an `env::adsr` shaping another
> over four and a half minutes. GEN_FORMAT.md §5a is the spec, `scripts/hostcheck` is the
> gate the hazards section asked for, and `src/thcNodeHost.*` is the
> host. Deltas from the sketch below:
>
> - **The spelling is the `.dsp` one, not `dsp::`.** The sketch wrote
>   `stage lfo dsp::sine`, and that is worse than what it was reaching
>   for: `sine` alone does not say which plugin directory to look in, and
>   the *family* is exactly what has to be judged before a module is let
>   near a control rate. So it is `osc::simple`, `math::mul`,
>   `env::adsr` — a category that is not `gen` or `xform` is a family
>   from the other world. The keyword stays `stage`, so §0's rule holds:
>   inside a chain everything is a stage, and the category says which
>   world it came from.
> - **The second interpreter is one small `thSynth` and the engine's own
>   tree.** thSynthTree reaches its synth for exactly one thing --
>   `getSampleRate()` -- which turns "write a control-rate host" into
>   "hand the existing one a different rate". One window of one sample,
>   fifty a second. That is not a degenerate case that happens to work:
>   a plugin divides by the rate it is handed and keeps what it
>   remembers in an `ARG_STATE` arg, so `freq = 0.05` is a twenty-second
>   cycle here and a very low note there and the plugin cannot tell.
>   Nothing about thSynth is duplicated, and the second synth does not
>   become `thSynth::instance()` -- that is set only when there is none.
> - **ARG_NODE landed as one more thing the param store reads through**,
>   beside the knob binding phase 2 built. An embedded node's output *is*
>   a `thArg`, so "evaluated at the moment the stage reads it" is not
>   machinery, it is the absence of machinery. Binding one releases the
>   other, so there is no precedence rule to remember.
> - **No scaling, on purpose.** An oscillator runs −1..+1 and `prob`
>   wants 0..1; `math::mul` and `math::add` are what a patch would use.
>   Three lines instead of one, in exchange for no hidden mapping and no
>   second meaning for the arrow depending on which side it lands on.
>   The composer chain inherits the DSP toolkit rather than growing a
>   worse copy of it, which was the point of the phase.
> - **A knob may drive a node's arg** (`in1 = @depth;`). Not in the
>   sketch, but leaving nodes out of the namespace phase 2 unified would
>   have made an LFO's depth the one number in a piece that could not go
>   on a slider.
> - **The category audit came out as the plan guessed**, with one
>   addition. `osc`, `env`, `math`, `logic`, `filt`, `misc` are shapes
>   over time and time at fifty a second is still time; `delay` and `fft`
>   count in *samples*, and a sample here is a fiftieth of a second, so
>   they would run and mean something nobody intended. Refused loudly, by
>   family. The addition is `osc::static`, refused by name: it draws from
>   the global generator, and a piece that used it would not replay.
>   There is nowhere to hand it the piece's seed and seeding `rand()`
>   would reach into the audio thread's copy of it.
> - **Determinism is a pure function of transport time**, not of how
>   often the host was called: windows fired by time t is
>   `floor(t * rate)`, with one stated exception -- a forward jump of
>   more than a few seconds is capped rather than caught up, because
>   three thousand windows inside one timer callback would stop the
>   program and a jump is honestly a jump. So a pause freezes the nodes, a rewind
>   replays them, and a late frame does not change what a piece sounds
>   like. `reset()` zeroes the state and output args rather than
>   rebuilding, because a rebuild would hand every `->` binding in the
>   piece a dangling pointer at the exact moment a replay began — and
>   zero is not merely plausible there, it is what `thArg::allocate`
>   value-initialises a node that has never run to.
>
> - **Every node runs every window**, rather than the audio thread's
>   walk from whatever declared itself ACTIVE. That walk exists because
>   a patch always has an oscillator in it and skipping the rest is what
>   makes a hundred voices affordable; down here the assumption is
>   simply false, since `math` and `logic` plugins are PASSIVE to a
>   module and a chain is entitled to hold nothing but arithmetic. Such
>   a graph fired once -- on stale recalc flags -- then froze, and after
>   a reset produced zeros forever. There is nothing to optimise over a
>   handful of nodes at one sample a window anyway.
> - **A node's arg names are checked against its plugin.** `thNode`
>   invents an arg that does not exist, which is right for a `.dsp` and
>   silent here: `frq` for `freq` gave an oscillator at zero and a piece
>   that did not breathe, with no error anywhere. Both ends of a wire,
>   both ends of an arrow. A module's ARG_STATE scratch is not a port
>   and neither is an input, so `lfo->last` and `lfo->freq` are refused
>   too.
> - **The document's stages and the scheduler's stopped being the same
>   list**, because a node is a `stage` in the file and nothing in the
>   event flow. Anything walking one while indexing the other goes
>   through `thcGenEdit::liveIndex` now; before it did not, and a piece
>   with an LFO at the top of a chain handed the canvas the wrong
>   stage's params. That is also why a knob driving a node drew no wire:
>   a node box has no `thcStage` for `eachWire` to read a binding off.
>   The host is asked instead.
> - One control-rate synth per *piece*, not per chain. Every plugin
>   keeps its registered arg indices in a file-scope global, which
>   assumes one `thPlugin` per module per process; a second
>   `thPluginManager` would `dlopen` the same `.so` and call
>   `module_init` again against a second one.
>
> Two things about the gates, both worth knowing.
>
> `hostcheck` compares the same graph cut into windows of one and into a
> window of two hundred. That is the drift the hazards section named:
> both hosts are the same `thSynthTree` walking the same `.so`, so what
> can differ between them is the *window length*, and a plugin that kept
> its phase in a local or read `buf[i - 1]` without remembering
> `buf[len - 1]` would be right at a thousand samples and wrong at one.
> It also refuses to pass on a signal that never moved, because two
> silences agree perfectly and prove nothing. Its first version had no
> passive-only graph in it, which is exactly where the ACTIVE-walk bug
> above was living: every case had an oscillator or an envelope pulling
> the arithmetic along behind it.
>
> And the first version of the replay gate was vacuous. It rendered
> `breath.gen` twice and diffed, which a host firing one window *per
> call* passes perfectly — the harness drives both renders with the same
> call pattern. Caught by deliberately breaking the clock and watching
> the gate stay green. It now also asks the question directly: one graph,
> one span of transport, reached in a hundred and fifty steps and in a
> single jump, same answer.

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

## Phase 4 — structural mutation: composers reshape instruments — LANDED (first half)

> **DONE, as far as the plan said to take it first.** "4 rides on all of
> it and should start life as one hardcoded structure edit end-to-end --
> prove rebuild-and-swap under the scheduler before any composer is
> allowed to breed a filter." Two edits are landed end to end, and no
> composer breeds anything yet.
>
> `THC_EV_PATCH` -- this channel becomes that instrument -- and
> `THC_EV_NODEARG` -- this constant inside its graph becomes that.
> `gen::swap` and `gen::reshape` emit them, `gen/reshape.gen` is the
> piece, GEN_FORMAT.md §5b is the spec. Notes worth keeping:
>
> - **Intents out, services in**, exactly as §9 of the handoff sketched.
>   A composer emits "this channel becomes `bell'" and the host does it;
>   no plugin holds a graph, and `bell' reaches the plugin as a resolved
>   name in a `THC_PARAM_INSTRSET` -- the same bargain a scale and a
>   preset already make, one noun further along.
> - **The voice-lifecycle question was answered by not answering it.** A
>   swap goes through the ordinary patch-load path -- and that path
>   replaces the channel, so a swap cuts whatever is sounding on it.
>   (Written the other way round here at first: `loadTree` promises the
>   outgoing channel is not freed under the audio thread, which is a
>   promise about lifetimes rather than about notes, and the two were
>   confused.) A swap is therefore a coarse edit that wants a slow clock.
>   A node-arg edit is the one that answers the question properly: it
>   lands on the channel's *prototype* tree, which thMidiChan.cpp says in
>   as many words the audio thread never reads, so sounding voices are
>   untouched with no swap and no command at all.
> - **Being an event is the whole rate limit**, and it cost nothing to
>   arrange: scheduled, sparse, replayed from the seed, drawn on the
>   roll. The plan predicted that and it turned out to be simply true.
> - **A rewind restores the declarations.** After a swap the channels no
>   longer say what the file says, so `reset()` re-applies every
>   instrument -- through the same call the loader makes, so there is one
>   answer to what a declaration means rather than a second one kept in
>   step by hand. Only when something moved; an ordinary rewind of an
>   ordinary piece reloads nothing.
> - **Four refusals worth having**, and the first draft got the most
>   important one half right. A node arg may be set only if it is
>   *already a constant* -- a whitelist on `ARG_VALUE`, not a blacklist
>   naming `ARG_POINTER`, because `thArg` has four types and the one the
>   blacklist missed was `ARG_CHANNEL`. `thNode::setArg` retypes an arg
>   to `ARG_VALUE` whatever it was, and
>   `thMidiChan::assignChanArgPointers` only re-points args still typed
>   `ARG_CHANNEL`, so `reshape { node = "fmap"; arg = "outmin"; }` against
>   `amb01.dsp` would have killed that channel's `@fmin` for the rest of
>   the session -- slider on screen, arg panel live, nothing moving,
>   nothing said. `NodeEditor::applyValueLive` asks the same question the
>   same way; it was there to be copied and was not. A module's
>   `ARG_STATE` scratch is refused too, and an arg the module never
>   declared would otherwise be invented by `thNode::setArg` and read by
>   nothing -- the same silence phase 3's mistyped node args produced,
>   arriving by a different door. The fourth is about swaps: only onto a
>   channel the piece declares an instrument for. Rebuilding a graph is
>   not like writing a chanarg, where the worst case is a number; it
>   throws away whatever was on the channel, and a `sink { channel = 5; }`
>   is in no declaration for a rewind to restore from.
> - **Applying an instrument had to become idempotent.** It was written
>   as a once-per-load call and phase 4 made it three: a swap applies one,
>   a rewind applies them all again. A knob bound into a chanarg is a
>   *push*, connected as its value is read, and the connection list only
>   ever grew -- so a swapped-away instrument went on driving the channel
>   it used to be on, alongside its replacement, until the piece was
>   closed. The connections now remember which channel they push into and
>   are dropped before the channel is wired again, which is the shape this
>   wanted from the start: applying an instrument says the same thing
>   however many times it is done.
> - **A swap has to record what is on the channel**, for two callers that
>   both got it wrong without it. `gen::swap` has a list of names and a
>   clock and cannot see what its sink is playing, so a list beginning
>   with the sink's own instrument rebuilt the graph into a copy of
>   itself on the opening tick -- every sounding voice cut, for no
>   change. And the window's "an unchanged instrument keeps its graph"
>   shortcut compares the generation (still ours after a swap) and the
>   declaration (unchanged in the file), so a reload after a swap kept
>   the swapped-in graph while believing the declared one was there,
>   whereupon applying the declaration's values failed on a chanarg the
>   wrong `.dsp` does not have and the whole file refused to load.
> - **The consent argument moved rather than vanished.** §4a's sentence
>   about chanargs being the whole of a composer's reach is still true
>   *of chanargs*; `reshape` is the different mechanism §9 promised
>   instead of a widening of that one. A piece reaching past a patch's
>   declared surface has said so in a line anyone can read, in a file
>   they can diff, at a rate its own event stream sets.
>
> Not here, and deliberately: add/remove/rewire a node, and the genetic
> convergence below -- `breed` and `evolve` over graphs, with the render
> pipeline as the fitness function. Those want a mutation vocabulary
> (`NodeGraph::canConnect`), a genome that serialises, and a shadow synth
> to judge it, which is the research branch the handoff files them under.
> What is proven here is the thing they were waiting on: an edit can be
> scheduled, delivered, drawn, replayed and refused.
>
> Every gate above was checked by breaking the fix and watching the gate
> fail -- the wired-chanarg refusal, the own-channel rule, the
> already-there no-op (which asserts the prototype tree is the *same
> object* afterwards, since "did nothing" is not visible any other way),
> the stale knob binding, and `reshape`'s ping-pong reflecting one step
> short of its walls rather than at them.

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
