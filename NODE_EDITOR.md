# The node editor

A visual editor for `.dsp` files: nodes, typed ports, wires, and the controls
that drive them. Reachable from **File → Node View** (Ctrl+N), and as a tab on
the patch page.

The format rules a writer has to obey — the grammar's constraints, what the
parser discards, why edits are spliced rather than re-emitted — are in
[DSP_FORMAT.md](DSP_FORMAT.md). This document is the editor's own model,
behaviour and layout.

## 1. The pieces

| Piece | Where |
|---|---|
| Arg direction in the plugin API | `thPlugin::ArgDir`, `regArg(name, dir)`, `argIsPort()` |
| Graph model, layout, hit-testing | `src/NodeGraph.{h,cpp}` |
| Canvas | `src/gui/NodeCanvas.{h,cpp}` |
| Position writer | `src/NodeLayout.{h,cpp}` |
| Everything-else writer | `src/NodeEdit.{h,cpp}` |
| Parameter panel | `src/gui/NodeParams.{h,cpp}` |
| Plugin palette | `NodeCatalog`, `src/gui/NodePalette.{h,cpp}` |

`NodeWindow` parses its own tree through `thSynth::parseTree()`, which neither
registers nor owns the result, so looking at a DSP cannot disturb the tree a
channel is playing.

`thPluginManager` loads plugins by name and cannot enumerate them, so
`NodeCatalog` walks the plugin directory instead — 62 plugins across 11
categories. Names come from the filesystem, which is instant; a plugin is only
`dlopen`ed when it is selected, to show its description and ports. Loading all 62
to draw a list would make the palette the slowest thing in the window.

## 2. The graph model

The corpus is small, and that shapes everything: median 13 nodes, 30 edges and 8
layers; worst 56, 134 and 17. No layout algorithm is going to struggle and no
canvas needs virtualisation.

**The io node is split into a source box and a sink box.** Treated as one vertex
it appears in a cycle in 89 of 92 files, which is an artefact; split, 87 of 92
are acyclic. It also reads better — signal flowing left to right from MIDI in to
audio out. Which arg goes to which half is
[a correctness question with a real answer](DSP_FORMAT.md#the-io-node), not a
display preference: before it was worked out, 1000 of the 1347 ports on the
audio-out box were phantoms.

**Five files have genuine feedback**: `effects/reverb01`,
`effects/whistlesynth01`, `noargs/dfb`, `noargs/smoothie`, `old/randompw`. Real
audio feedback loops, entirely legitimate. Layered layout handles them the usual
way — pick a feedback arc set, reverse those edges for layering, draw them as
back-edges.

**`ARG_STATE` args never appear.** 69 args are internal plugin state and no
shipped `.dsp` references one. Wiring a delay line's internal buffer invites
exactly the kind of divergence that makes `old/test.dsp` peak at 1.75e5.

Two invariants worth not breaking:

- **`NodeGraph::canConnect` owns the connection rules; `NodeEdit` writes what it
  is told and judges nothing.** The rules are phrased over *boxes*, not over
  nodes — which is what makes "a node cannot feed itself" expressible at all,
  given that the io node is one node and two boxes.
- **`NodeGraph::edgeCurve` feeds both the drawing and `edgeAt`**, so what is
  drawn and what can be clicked are the same shape by construction. Describing
  the same cubic twice is how "clicking a wire selects a different wire" happens.

## 3. Interaction

Drag boxes, Ctrl+wheel to zoom, hover for detail.

**Wiring.** Drag from a port to another port. The rubber band snaps to the port
it would land on and turns red when the drop would be refused, so the refusal is
visible before the button comes up rather than after. Output-to-input and
input-to-output are the same gesture. Clicking a wire removes it.

**When edits reach the file.** Values, wires and positions apply to the on-screen
graph immediately and reach disk only on Save — a wiring gesture that produced no
visible wire until a save would be unusable, and a file rewritten on every
gesture would be alarming.

**Adding and deleting nodes is the exception: those write immediately and
reopen.** A node's ports come from its plugin, the only thing that knows them is
a parse, and a node that exists on the canvas but not on disk has no honest way
to be drawn. Revert still undoes them.

**Fit is a button, not a behaviour.** An earlier revision fitted the graph to the
window on opening; that was removed, because shrinking every patch until it fits
makes a wide one unreadable and disguises the fact that it is wide. Fit never
magnifies past 1:1 — a four-node patch blown up to fill the window looks broken,
and the point is only to bring an oversized one down.

## 4. Controls

A DSP keeps the settings meant to be played with in top-level `@name` blocks, and
nodes read them as `in1 = @blim`. Showing that as greyed-out text on whichever
node happened to read it buried the most interesting part of the patch, so **each
block is a node of its own** — a box with a slider, one output, and a wire to
every arg that reads it. `in1 = @blim` is a wire like any other, and cutting it
works like any other.

### Attached, not free-standing

**197 of 206 controls drive exactly one parameter**, and no control anywhere
drives two parameters on the same node. So the ordinary case is a control that
belongs to precisely one box, and it is drawn as a strip stacked on top of that
box — label, track, value on one line, no title bar. A host carries between one
and five, median one.

**Above, not beside.** Beside was the first attempt and it cost every column its
own width again: `ts1.dsp` went from 2148 pixels wide to 3058 while using only
352 of them vertically. A graph runs left to right, so width is the axis that
runs out and height is the one going spare. A strip the same width as the box it
sits on costs no width at all.

**The wire is still drawn.** Dropping it was a mistake: sitting a strip on a node
says which node a control belongs to, but a node has several inputs and adjacency
cannot say which one. That is precisely what the wire is for. It gets a
vertical-tangent curve rather than the usual horizontal one, because it drops a
short distance rather than crossing the canvas.

### The nine that are shared

A control read by several nodes cannot attach to any one of them. Those stay
boxes with visible wires — and that is the useful reading rather than a fallback:
**a control still drawn as a box is one that several things share.** The corner
says `@waveform  shared x4` so it is clear that is the reason, not a failure to
attach.

They were also being stranded. Layering gives an input-less box layer 0, so
`@waveform` in `organ0.dsp` sat at the far left with four long wires crossing the
patch to reach nodes in the middle. A free-standing control is now placed just
before its earliest consumer; nothing points at it, so moving it right cannot
make any edge run backwards.

### What that bought

```
                       mean         tallest         widest
controls as boxes  1703 x 663    2656 (aspect2)      3336
attached beside    1822 x 519    1486                3386
attached above     1703 x 542    1486                3336
plus a tighter gap 1502 x 542    1486                2920
```

18% shorter for exactly the same width. `LAYER_GAP` then came down from 70 to 44:
it was 70 because a control in layer 0 could have a long wire crossing a column,
and they are strips on their hosts now. On a 17-layer patch that is 440 pixels.

### Retyping one

Right-click a control — its box, or the strip it is drawn as — for its range,
label and group. The range is the thing a slider can neither show nor change: a
cutoff declared `0..1` has a slider that reaches a tenth of the filter, and
nothing on the canvas says so or offers to fix it.

Four lines, one write. `.min`, `.max`, `.label` and `.group` are spliced
together and the file renamed into place once, so a range change is not four
chances to leave a half-edited file behind. A line the file does not have is
added at the end of the block: the parser only requires that `@x.min` come
*after* `@x`, so the canonical order `addControl` writes is for a reader rather
than a requirement, and reordering someone's file to match it would be an edit
they did not ask for. An empty label or group **removes** the line rather than
writing `""` — a control that never had a label and one whose label was cleared
should be the same file.

**Narrowing a range clamps the value.** Otherwise the slider cannot reach what
the file says and moves the instant it is touched, which reads as a change to
the sound coming from nowhere; clamping makes it happen at the moment the range
changed, which is the moment it can be understood. It is one-way, and a
round-trip check has to know that: a range widened and put back leaves the file
byte for byte, and a range narrowed and put back cannot.

**A range is a number like any other**, and gets the treatment
[DSP_FORMAT.md](DSP_FORMAT.md#splice-do-not-re-emit) already describes for one.
It is compared by value rather than by spelling, so `@lvl.max = th_max` survives
a write of 1 with its six characters intact — the same reason 229 uses of
`th_max` and `th_min` survive a save anywhere else. Arithmetic is refused
outright rather than folded to whatever it evaluates to today, and refusing the
maximum refuses the minimum with it: half a range this editor cannot read back
is worse than none of it. No shipped `.dsp` spells a range either way — all 412
range lines are literals — so `dspnew` writes the files that do.

**The name is not editable here.** Renaming would have to rewrite every
`in1 = @old` in the file as well as the block, which is `removeControl` and
`addControl` and a different thing to offer. The field is shown and
insensitive, so the dialog still says which control it is about.

Like adding a node, this writes immediately and reopens rather than waiting for
Save. A range decides how the strip is drawn and what the slider can reach, and
both come from a parse — a range that had changed in the file and not on the
canvas would be a slider lying about where its own ends are. Revert still
undoes it.

### Groups

`.group` lets four envelope sliders draw as one titled block. Two things the
shipped patches forced:

- **A group is per host.** `ts1.dsp`'s "Filter" is a cutoff on one node, a
  resonance on another and an amount on a third. Those strips cannot be one block
  because they are not on one box. Each host shows its own slice.
- **So a heading only appears when a host shows more than one member of the
  group.** Titling three separate single sliders "Filter" three times is noise
  where the label already says what each one is.

## 5. Authoring

The palette lists every plugin on disk grouped by category, which is not an
invention — a `.dsp` spells a plugin `osc::simple`, so category-then-name is how
the format already thinks about it.

**Controls are the palette's first entry, above the plugin categories and on
their own**, because a control is the one thing there that is not a plugin:
`@blim` is a block in the file, not something in `plugins/`. Filing it under a
made-up category would say otherwise.

Adding one asks for a name, range, label and group, because unlike a plugin
nothing about a control is implied by picking it. The range especially has no
sensible default — 0 to 1 is right for a mix and useless for a filter cutoff —
and getting it wrong means a slider that cannot reach the value you want. The
dialog loops rather than validating once, so a rejected name can be corrected
instead of throwing the whole thing away.

It is the same dialog that retypes an existing control, with the name fixed and
the value left out: the two disagree about which fields to show and agree about
what makes an answer unusable, and the validation is the half worth having in
one place. A name the lexer will not read back, a label with a quote in it and
an inverted range are refusals whichever operation asked.

**New** writes the smallest file that loads and builds up from there; the result
is indistinguishable from a hand-written file because it is produced by the same
splicer that edits one.

## 6. Live editing

Moving a control pushes the value straight into the running synth, so it is
audible on notes already sounding — the same `thArg::setValue(float)` the
keyboard's sliders have always used: a single atomic store, no reallocation, safe
to call from the GUI thread while the audio thread reads.

The window has to be attached to a channel for this, so File → Node View opens on
the channel of the tab you are looking at, and the status bar says `live on
channel 3` or `not attached to a channel`.

Three kinds of edit, and they behave differently:

- **Controls** are channel args, shared by every note on the channel. Changing
  one is heard immediately, mid-note.
- **A node's own value** is copied into each note at note-on, so changing it
  changes the *next* note and leaves ringing ones alone. The status bar says
  `(next note)` rather than pretending otherwise.
- **Wire changes** are not applied live at all; they need the graph rebuilt.

## 7. Why the layout will not get much narrower

`ts1.dsp` is an eleven-stage signal chain at 1888 pixels; the deepest in the
corpus is seventeen layers at about 2900. Twenty-five of the eighty-one graphs
are wider than 1600. That is what the patches *are*, and three separate levers
were measured and found nearly exhausted.

**The layering is already optimal.** Longest-path puts every node at its
critical-path depth, and no drawing in which every wire goes forwards can have
fewer columns than the critical path. `scripts/dsplayout` checks it: 80 of 81
graphs are already at the floor. Coffman-Graham and friends bound the number of
nodes *per layer*, which makes a graph taller and can make it deeper — the
opposite of what is wanted here.

**Boxes cannot shrink much either.** A box must fit its longest input and output
port names on the same row. Worst in the corpus is 18 characters together, median
6 to 9, and the header carries the node name plus the plugin name, which already
clips. Perhaps 12% is available, on a 172-pixel layer of which 128 is the box.

**Wrapping works and is off anyway.** `NodeGraph::setWrapWidth` cuts the column
sequence into bands and stacks them, the way a long circuit is drawn on several
rows of a schematic, and it hits the target exactly:

```
  wrap 0      mean 1502 x  542   widest 2920   25 over 1600   0 wrapped
  wrap 1900   mean 1359 x  595   widest 1888   15 over 1600  10 wrapped, 166 jumps
  wrap 1500   mean 1058 x  752   widest 1372    0 over 1600  41 wrapped, 462 jumps
```

But every wire crossing a band boundary becomes a long return from the right edge
to the left, one band down, and the cut profile says that is a bad trade:

```
dsp/ts1.dsp       11 layers  cuts: 4 6 7 7 6 7 5 4 3 3
dsp/old/bd9.dsp   17 layers  cuts: 44 42 38 35 33 29 25 23 18 16 12 11 9 7 5 3
```

`bd9.dsp` is the widest graph in the corpus and therefore the one that most needs
wrapping — and splitting it in half costs 23 long return wires. These patches are
not chains; they are broad fans, dozens of parallel paths from the input to the
mixer. Wrapping trades a scrollbar for a tangle.

The code stays, defaulted off, and `dspgraph -w 1500` runs every layout invariant
against it so it is not untested code pretending otherwise. If a patch ever turns
up that is deep and *narrowly* connected, it is one call away.

## 8. The checks

None of these are CTest gates — they all take a corpus argument, so they are run
by hand rather than by `ctest`.

| Harness | Covers |
|---|---|
| `dspgraph` | every wire on a correctly-facing port, no double fan-in, no overlapping boxes, no `ARG_STATE` exposed, hit-testing on boxes and ports, attached controls against their hosts, shared controls laid out before what they drive, io-node args partitioned across the two halves, probe panels |
| `dspwrite` | values and wires cut and restored across the corpus, byte-identical; every control's range, label and group retyped and restored, and every value clamped by a range narrowed past it |
| `dspnew` | builds files from nothing: adds and removes one node of every plugin in the catalogue, retypes a control it just added, writes the range spellings no shipped file uses, then renders audio from what it built |
| `dsplayout` | layer counts against the critical-path floor, and the wrap-mode cut profile |
| `dsplive` | that moving a control changes the sound of a ringing note |
| `dspab` | two renders compared bitwise, for changes meant to be inaudible |

Three things worth repeating about how these are written:

- **A round-trip check must scramble first.** Asserting that positions survive a
  save is worthless if the test writes out the positions `layout()` just
  computed, since a save that silently did nothing still passes.
- **A parse cannot tell you a line went in the wrong place.** The grammar cares
  only that `@x.min` follows `@x`, so a `.group` that landed past the block's
  blank separator reads back perfectly and looks wrong. `dspnew` asserts the
  block is contiguous, which is the only way that break shows up — it was
  written, confirmed to pass with the bug in, and the assertion added because
  of it.
- **`dspnew`'s last assertion is the point.** Everything above it can pass while
  the result is a file that loads and makes no sound, which is not authoring.
- **`dsplive` reports its non-effects separately rather than as errors.** 151
  controls change the sound of a ringing note and 55 do not — `@a`, `@d` and `@r`
  are envelope times, so by the time the change lands the note is in sustain and
  there is correctly nothing to hear.

**Layout quality is subjective and cannot be unit-tested**, and neither can the
feel of a gesture. The bugs that matter here are visual and interactive, and
finding them means sitting in front of it.

## 9. Still ahead

- **`dspcheck` should grow a "load this and process it" check** for anything the
  editor writes.
- **`thSynthTree::buildSynthTree` walks the graph recursively with a `recalc`
  flag rather than a proper topological order.** An editor makes it easy to build
  pathological graphs.
- **A `@chanarg` cannot be driven by a node.** "The knob moves on its own" needs
  engine work; see [DSP_FORMAT.md](DSP_FORMAT.md#modulation-is-not-a-special-case).

## 10. Two things that cost a day each

**Args that no plugin declared.** The panel and the canvas are built by separate
passes, so `dspgraph` cross-checks them — and they disagreed. `filt::moog`
produces `out_low`, `out_high` and `out_bandpass` and registered none of them;
they existed only as string lookups inside the callback. `math::sin` registered
nothing at all, and `input::alsa` likewise. Six wires were being silently
dropped. If you are measuring anything over "all args", registered is not the
same set as used.

**A stale header next to a test source.** A crash in a throwaway harness turned
out to be an old `NodeGraph.h` sitting beside the test, so it compiled against
the old `Box` and linked the new one. `#include "NodeGraph.h"` resolves relative
to the including file first.
