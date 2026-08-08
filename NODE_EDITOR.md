# Scoping the DSP node editor

Measured against the 92 shipped `.dsp` files and the 62 built plugins, on
`node-editor-scope` off `gtkmm4-scope`.

The headline: **the thing I called the blocker in the original survey turns out
to be already solved, and nobody noticed.** The rest is real work but
unremarkable — a canvas widget, a graph model, and a `.dsp` writer.

## 1. Port direction: already there

`REVIVAL.md` §4a says the editor is blocked because `plugin->regArg("a")` and
`plugin->regArg("out")` are identical calls, so nothing in the plugin API says
which args are inputs and which are outputs, and that fixing it means a plugin
API v5 touching all 66 plugins.

That is true of the *API*. It is not true of the *code*. Every plugin already
declares direction, in the enum it indexes `args[]` with:

```c
enum {IN_A, IN_D, IN_S, IN_R, IN_P, IN_TRIGGER, IN_RESET,
      OUT_ARG, OUT_PLAY, INOUT_POSITION};
```

Cross-checking that convention against what each plugin actually *does* —
whether it calls `allocate()` on an arg (writes it) or `getBuffer()`/`operator[]`
(reads it):

```
enum-prefix convention vs behaviour:  302 agree, 0 disagree
args not following the convention:    3
```

**302 of 305 args, zero disagreements.** The three stragglers are
`logic::not`'s `IN`, `misc::decibel`'s `DB` and `mixer::fade`'s `OUT` — the
convention without the underscore suffix, obvious on sight.

So the metadata does not need inventing, only exposing. Two ways:

- **Generate it.** A script reads the enums and emits a table the editor loads.
  No plugin changes at all. Fastest, and the cross-check above is the proof it
  is safe.
- **API v5 properly.** `regArg(name, thPlugin::IN|OUT|STATE)`, mechanically
  applied from the same extraction, three by hand. Better long-term — the
  direction lives with the plugin, and the same call can carry a description
  and a sane range for the UI.

The second is barely more work than the first because the first does the
analysis anyway. It wants an `lib_major` bump.

### The `INOUT_` args are not ports

69 args are internal state — `delay::echo`'s ring `buffer` and `bufpos`,
`env::adsr`'s `position`, every filter's `last`. They are allocated and read
by the plugin across windows, and **no `.dsp` in the corpus ever references
one**. They must not appear on the canvas: nobody wants to wire a delay line's
internal buffer, and letting them try invites exactly the kind of divergence
that makes `old/test.dsp` peak at 1.75e5.

The `INOUT_` prefix identifies them exactly.

## 2. What the graphs actually look like

| | median | max |
|---|---|---|
| nodes | 13 | 56 |
| edges | 30 | 134 |
| layers (longest chain) | 8 | 17 |

Small. No layout algorithm is going to struggle, and no canvas is going to
need virtualisation or level-of-detail.

**87 of 92 are acyclic** — but only once the `io` node is split. That node is
both the MIDI source (`note`, `velocity`, `trigger` are read by other nodes)
and the audio sink (`out0`, `out1`, `play` are written to it). Treated as one
vertex it appears in a cycle in 89 of 92 files, which is an artefact. The
editor's model should split it into a source box and a sink box; that is also
how it reads best on screen — signal flowing left to right from MIDI in to
audio out.

The remaining **5 have genuine feedback**: `effects/reverb01`,
`effects/whistlesynth01`, `noargs/dfb` (the name is a clue), `noargs/smoothie`,
`old/randompw`. Real audio feedback loops, entirely legitimate. Layered layout
handles them the usual way — pick a feedback arc set, reverse those edges for
layering, draw them as back-edges.

## 3. Modulation is already how these work

The "well beyond initial scope" item is mostly already built.

The language draws no distinction between a constant parameter and a signal.
A node input can be bound to a literal, to a `@chanarg`, or to another node's
output, and the last of those is used constantly. From `ts1.dsp`:

```
node map1 env::map {
    in     = env->out;          # envelope drives the mapping
    outmin = cutcalc->out;      # and the range is itself computed
    outmax = cutcalc2->out;
};
node filt filt::res2pole2 {
    cutoff = map1->out;         # filter cutoff modulated by the envelope
};
```

That *is* parameter modulation, in the engine, today. Dragging a wire from an
oscillator's output onto an input that currently reads `@cutoff` needs no
engine change whatsoever — it changes that arg from `ARG_CHANNEL` to
`ARG_POINTER`, which the parser, the graph builder and the audio path all
already handle.

The one thing genuinely *not* supported: a `@chanarg` itself being driven by a
node. Chanargs are declared `@x = <constant>` and written by the GUI and MIDI
controllers, and nothing can drive one from the graph. So "the knob moves on
its own" would need engine work; "this parameter varies with an LFO" would not.
Worth knowing which of the two you meant, because they are very different
amounts of work.

## 4. Round-tripping: the awkward part

The editor has to write `.dsp` files back, and the parser throws away things a
naive re-emit would not restore:

- **Comments.** 59 of 92 files have them, 391 in total, and they are the
  author's notes — `# multiply the filter start by famt`, attribution headers,
  dates. Losing them on first save would be vandalism.
- **Units.** 283 values are written `5 ms` or `90%`. The lexer folds these to
  raw floats, so `a = 5 ms` comes back as `a = 220.5`. Correct, unreadable.
- **Synthesised args.** `buildArgMap()` calls `setArg(name, 0)` for every arg a
  plugin registered but the `.dsp` did not mention. Re-emitting the in-memory
  model would write out dozens of `reset = 0;` lines nobody authored.
- **Arithmetic.** Only 8 right-hand sides across the corpus use it, so this one
  barely matters.

Two workable approaches:

- **Edit the text, not the model.** Keep the source and splice changes into it,
  preserving everything untouched. Fiddly, but comments survive and diffs stay
  small and reviewable — which matters a lot for files under version control.
- **Re-emit, but track provenance.** Mark each arg as authored or defaulted, and
  keep comments and units on the parsed nodes. More invasive to `thArg` and the
  grammar, but the editor then owns the file properly.

I would start with splicing. It is less satisfying but it cannot lose data, and
losing someone's `.dsp` comments is the kind of thing that stops them using the
editor entirely.

**Node positions** want storing too. A structured comment — `# @layout freq 120
40` — keeps files loadable by the current parser and by every existing tool,
and degrades to a harmless comment if the editor is never used again.

## 5. Where it should live

In-app, as a gtkmm-3 `DrawingArea`. That answer changed with the gtkmm-3 port:
the Cairo drawing path is now proven in this tree, the `Keyboard` widget is a
working precedent for custom drawing plus mouse hit-testing, and the real
payoff of an in-app editor is hearing a change the moment you make it.

A separate web tool would be nicer to build and would need a second parser and
a process boundary, for a worse result.

## 6. Suggested order

1. **Port metadata.** Extract directions, hide `INOUT_`. Either the generated
   table or API v5. Nothing else can start without it.
2. **Graph model and layout.** Build a display graph from `thSynthTree` — split
   the `io` node, layer it, break feedback arcs. Testable headlessly against all
   92 files: every node placed, no overlaps, back-edges identified.
3. **Read-only canvas.** Draw nodes, ports and edges. No interaction. This is
   the point at which it is worth looking at, and where the layout gets judged.
4. **Interaction.** Drag nodes, pan and zoom, hit-test ports.
5. **The `.dsp` writer**, with position comments. Round-trip every shipped file
   and diff — a file with no edits must come back byte-identical.
6. **Editing.** Create and delete edges, add and remove nodes. Only now does it
   need the writer to be trustworthy.
7. **Parameter controls.** Chanargs already carry `widget`, `min`, `max` and
   `label` from the `.dsp`, and `ArgTable` already renders exactly that — the
   panel largely exists.

Steps 1–3 are the ones worth doing first regardless: they are independently
useful, they are all headlessly testable, and step 3 answers the question that
actually matters, which is whether an auto-arranged thinksynth graph is legible.

## 7. Risks

- **Layout quality is subjective and cannot be unit-tested.** 17 layers of 56
  nodes may still look like spaghetti when correctly laid out. Step 3 exists to
  find that out cheaply.
- **The editor can produce graphs the engine mishandles.** Feedback loops
  already exist in 5 files, and `thSynthTree::buildSynthTree` walks the graph
  recursively with a `recalc` flag rather than a proper topological order. An
  editor makes it easy to build pathological graphs; `dspcheck` should grow a
  "load this and process it" check for anything the editor writes.
- **Verification needs you.** As with the keyboard, the bugs that matter here
  will be visual and interactive. Both real bugs in the gtkmm-3 port came from
  you using it, not from anything catchable headlessly.

---

## 8. Progress

Implemented on `node-editor` off `gtkmm3-port`. Steps refer to §6 above.

| Step | State | Where |
|---|---|---|
| 1. Port metadata | done | `thPlugin::ArgDir`, `regArg(name, dir)`, `argIsPort()` |
| 2. Graph model and layout | done | `src/NodeGraph.{h,cpp}` |
| 3. Read-only canvas | done | `src/gui/NodeCanvas.{h,cpp}` |
| 4. Interaction | done | drag, Ctrl+wheel zoom, hover; hit-testing in `NodeGraph` |
| 5. The `.dsp` writer | positions and values | `src/NodeLayout.{h,cpp}`, `src/NodeEdit.{h,cpp}` |
| 6. Editing | wires done | `NodeEdit::connect`/`disconnect`, canvas drag |
| 7. Parameter controls | done | `src/gui/NodeParams.{h,cpp}` |

Reachable from **File → Node View** (Ctrl+N). `NodeWindow` parses its own tree
through `thSynth::parseTree()`, which neither registers nor owns the result, so
looking at a DSP cannot disturb the tree a channel is playing.

Verified headlessly by `scripts/dspgraph` over all 92 files (81 build, 11 do not
load at all): every wire attached to a correctly-facing port, no double fan-in,
no overlapping boxes, no `ARG_STATE` exposed, clicking a box's centre picks that
box, clicking a port's drawn position picks that port, and positions surviving a
write/read/apply cycle without changing any other line.

### On the writer

§4 recommended splicing over regenerating, and that has held up. `NodeLayout`
writes only `# @layout` comments: it reads the file, drops its own lines, copies
everything else through untouched and appends a fresh block. Nothing is ever
reconstructed from the parsed model, so the 391 comments, the 283 unit-suffixed
values and the folded arithmetic cannot be lost by it.

Two things that only showed up in practice:

- **The block has to be idempotent.** Its own header comments did not match the
  prefix being stripped, so every save added two more lines. Anchoring the whole
  block — prose included — on one prefix fixed it. The round-trip check caught
  this on its first run.
- **Scramble before checking.** Asserting that positions survive a round-trip is
  worthless if the test writes out the positions `layout()` just computed, since
  a save that silently did nothing still passes. The check now moves every box
  somewhere arbitrary first.

Rewriting node definitions — the writer step 6 actually needs — is still ahead,
and remains the highest-risk piece of this work.

### Deviation from the suggested order

Doing step 7 before step 6. Parameter editing needs only a narrow splice into
one line — the right-hand side of a single `arg = value` — where edge editing
needs the full node-definition writer up front. Since step 6 needs that same
splicer anyway, building it against the smaller problem first is the cheaper way
to find out where the writer is wrong.

### What the grammar actually allows

Writing values turned out to be constrained in ways reading never revealed:

- The lexer's number pattern is `[0-9]+(\.[0-9]*)?`. **No exponent.** A value
  that needs one cannot be written at all — `NodeEdit` refuses rather than
  emitting something that will not parse.
- **Negatives** exist only as a unary-minus rule over that. Eight in the corpus.
- `5 ms` is `5 * TH_SAMPLE / 1000` and `50%` is `50 * TH_MAX / 100`. Both are
  exactly invertible, so a value written with a unit keeps it.
- **229 uses of `th_max` and `th_min`**, plus `th_range`, `th_midimax` and
  `th_sample`. A writer that did not recognise these would turn `inmax =
  th_max` into `inmax = 1` on the first save of any file containing one.
- Eight right-hand sides are arithmetic. `NodeEdit` refuses those too — an
  editor that silently replaced someone's `a * 2` with a constant would be
  doing exactly the damage splicing exists to prevent.

Two things fell out of building it that are worth recording:

**Exact float equality does not work.** libthink is built with `-ffast-math`,
which lets the compiler turn the grammar's `× TH_SAMPLE / 1000` into a multiply
by the reciprocal. So `0.5 ms` is held as 22.0500011 where honest arithmetic
gives 22.0499992 — a couple of ULP apart, and enough that re-deriving the
literal exactly is impossible. Demanding it rewrote `0.5 ms` as
`0.50000003 ms`. The comparison is now four ULP wide. `REVIVAL.md` lists
dropping the global `-ffast-math` as outstanding; this is one concrete thing it
costs.

**The guarantee is about untouched values, not about round trips.** Once a
value has genuinely been changed and changed back, `th_max` comes back as `1` —
the writer has no memory of how a number used to be spelled, only of when it
does not need to touch one at all. So `scripts/dspwrite` asserts the property
that matters: a write of the value already there changes no byte of the file.
1942 of those across the corpus, every one byte-identical.

### Args that no plugin declared

The panel and the canvas are built by separate passes, so `dspgraph` now
cross-checks them — and they disagreed. `filt::moog` produces `out_low`,
`out_high` and `out_bandpass` and registered none of them; they existed only as
string lookups inside the callback. `math::sin` registered nothing at all.
`input::alsa` likewise. Six wires were being silently dropped. All three now
register properly, verified sound-identical by the new `scripts/dspab`.

This qualifies §1: "302 of 305 args agree, zero disagree" was measured over
*registered* args, so args conjured at callback time were never in the sample.

### Wiring

Drag from a port to another port. The rubber band snaps to the port it would
land on and turns red when the drop would be refused, so the refusal is visible
before the button comes up rather than after. Dragging output-to-input and
input-to-output are the same gesture. Clicking a wire removes it.

Wire edits apply to the on-screen graph immediately but reach the file only on
Save, alongside the pending values — a wiring gesture that produced no visible
wire until a save would be unusable, and a file rewritten on every gesture
would be alarming.

Two things this settled:

**"A node cannot feed itself" is false for this format.** It was in the writer
first, and three shipped DSPs failed the round trip on it: `jp420`, `organ2`
and `jp420-B` all contain `ionode.fade78 = ionode->velocity`. The io node is one
node in the file and two boxes on screen, so the rule only makes sense phrased
over boxes. It lives in `NodeGraph::canConnect` now; `NodeEdit` writes what it
is told and judges nothing.

**The curve is in the model, not the canvas.** `NodeGraph::edgeCurve` feeds both
the drawing and `edgeAt`, so what is drawn and what can be clicked are the same
shape by construction. Describing the same cubic twice is how "clicking a wire
selects a different wire" happens.

`scripts/dspwrite` now cuts and restores every wire in the corpus:

```
2877 wires cut and restored, 2877 reconnects to where they already
went, all byte-identical
```

The reconnect being byte-identical is what makes it safe: all 3476 connections
in the corpus are spelled `name->port` with no spaces, so rewriting one
reproduces the original text exactly. That is also why `disconnect` rewrites the
line to `= 0` rather than deleting it — to the engine the two are the same, but
only the rewrite keeps the line's position, indentation and trailing comment,
and only the rewrite makes a reconnect restore the file.

### Still ahead

Adding and removing whole nodes, which needs the plugin list and a way to name
things. And chanarg editing: most DSPs keep every setting worth touching in the
channel block, and the panel shows those values but will not yet change them —
that means writing the `@name = ...` block rather than a node block.
