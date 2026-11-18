# The node editor

A visual editor for `.dsp` files: nodes, typed ports, wires, and the controls
that drive them. It is the main window's **Patch graph** pane, showing the
channel picked in Channels. Its palette and parameter panel start collapsed;
the first two toggles on its toolbar open them.

The format rules a writer has to obey — the grammar's constraints, what the
parser discards, why edits are spliced rather than re-emitted — are in
[DSP_FORMAT.md](DSP_FORMAT.md). This document is the editor's own model,
behaviour and layout.

## 1. The pieces

| Piece | Where |
|---|---|
| The editor: the patch's text, its edits, Save and Revert | `src/gui/NodeEditor.{h,cpp}` |
| Undo and Redo's steps, and what one changes in a running synth; no toolkit | `src/NodeHistory.{h,cpp}` |
| Arg direction in the plugin API | `thPlugin::ArgDir`, `regArg(name, dir)`, `argIsPort()` |
| Graph model, layout, hit-testing | `src/NodeGraph.{h,cpp}` |
| Canvas: the drawing and the gestures, no toolkit | `src/NodeCanvas.{h,cpp}` over `src/CanvasContent.{h,cpp}` |
| Canvas: the gtk widget around it | `src/gui/NodeCanvasWidget.{h,cpp}`, `src/gui/GraphCanvas.{h,cpp}` |
| Position writer | `src/NodeLayout.{h,cpp}` |
| Everything-else writer | `src/NodeEdit.{h,cpp}` |
| Parameter panel: what a param is | `src/NodePanel.{h,cpp}` over `src/PanelModel.h` |
| Parameter panel: the gtk widget | `src/gui/NodeParamsView.{h,cpp}` over `src/gui/PanelView.{h,cpp}` |
| Plugin palette | `src/NodeCatalog.{h,cpp}`, `src/gui/NodePalette.{h,cpp}` |
| The browser's editor: the page's half, its forms and menus | `wasm/web/nodeview.js` |
| The browser's canvas shell: drawing, pointer, panning and pinch | `wasm/web/canvasview.js` |
| The browser's panels drawn | `wasm/web/panel.js` |
| The browser's graph, canvas and edits: the module's exports | `wasm/web/thinknode.cpp` |
| The browser's panel family, and what a node panel's edit means | `wasm/web/thinkweb.cpp` (`tw_panel_*`, `tw_node_propose`) |

`NodeEditor` parses its own tree through `thSynth::parseTreeText()`, which neither
registers nor owns the result, so looking at a DSP cannot disturb the tree a
channel is playing.

**The browser's editor** is the page's Patch graph pane, on the solo page and
in a room. Its File list is the `.dsp` files the page has: the one playing in
patch mode, or the ones a piece's instruments name. An edit is spliced into
that text with the same `NodeEdit::Text` and `NodeLayout::Text` calls the
desktop makes; on the solo page the patch then loads again, and in a room the
splice is the shared document's, so every peer has it. There is no Save, since
the text is the file, and Undo is the solo page's alone so far (see below).
When what is playing uses only the shipped patches there is nothing to edit,
and the pane says so.

`thPluginManager` loads plugins by name and cannot enumerate them, so
`NodeCatalog` walks the plugin directory instead, and the palette shows the
categories whose plugins are nodes. Names come from the filesystem, which is
instant; a plugin is only `dlopen`ed when it is selected, to show its
description and ports. Loading every one to draw a list would make the palette
the slowest thing in the window.

## 2. The graph model

The corpus is small, and that shapes everything: a typical patch is a dozen
or so nodes in under ten layers, and the largest a hundred-odd boxes. No
layout algorithm is going to struggle and no canvas needs virtualisation.

**The io node is split into a source box and a sink box.** Treated as one vertex
it appears in a cycle in nearly every file, which is an artefact; split, nearly
every file is acyclic. It also reads better — signal flowing left to right from MIDI in to
audio out. Which arg goes to which half is
[a correctness question with a real answer](DSP_FORMAT.md#the-io-node), not a
display preference: before it was worked out, most of the ports on the
audio-out box were phantoms.

**One file has genuine feedback**: `scripts/guard/feedback.dsp`. Layered
layout handles a cycle the usual way — pick a feedback arc set, reverse those
edges for layering, draw them as back-edges — and this is the only graph left
to run that on, which is why it is kept.

There used to be more. `effects/reverb01` had a cycle the effect format now refuses
outright and `effects/whistlesynth01` wrote `fm = osc1->out`, which
`osc::fmop` has an arg for; `fx/resynth.dsp` replaced the second.
`noargs/smoothie` and `old/randompw` became `dsp/waveguide.dsp` and
`dsp/sandh.dsp`, both rebuilt so that the loop sits inside a plugin instead of
around the graph. `noargs/dfb` could not be — an oscillator FM'd by a delay
line fed from itself is the cycle — so it moved to `scripts/guard/` as the
specimen rather than being deleted.

**`ARG_STATE` args never appear.** They are internal plugin state, and no
shipped `.dsp` references one. Wiring a delay line's internal buffer invites
exactly the kind of divergence that made an old test patch peak at 1.75e5.

Two invariants worth not breaking:

- **`NodeGraph::canConnect` owns the connection rules; `NodeEdit` writes what it
  is told and judges nothing.** The rules are phrased over *boxes*, not over
  nodes — which is what makes "a node cannot feed itself" expressible at all,
  given that the io node is one node and two boxes.
- **`NodeGraph::edgeCurve` feeds both the drawing and `edgeAt`**, so what is
  drawn and what can be clicked are the same shape by construction. Describing
  the same cubic twice is how "clicking a wire selects a different wire" happens.

## 3. Interaction

Drag boxes, Ctrl+wheel to zoom, hover for detail. A drag with the middle
button, or with Space held, moves the view rather than anything in it, in the
browser's editor too, and there a finger on nothing does the same: held still
a moment first, it draws a band; two fingers pinch. A
control's strip shows a few letters of its label and its value in five
characters, `14.1k'; hovered, it says the whole label and the value as the
parameter panel spells it, with its range, in the browser too
(`NodeCanvas::tipAt`).

**The toolbar.** New, Save As and Revert are in its File menu; then Save,
Undo and Redo, Delete, Auto-arrange, and the zoom steps -- out, 1:1, in and
Fit. The browser's has Add node and Add control, Delete, Arrange and the same
zoom steps; its File list picks which of the page's files to show, and it has
no New, Save As or Revert, since those files are the page's.

**Wiring.** Drag from a port to another port. The rubber band snaps to the port
it would land on and turns red when the drop would be refused, so the refusal is
visible before the button comes up rather than after. Output-to-input and
input-to-output are the same gesture. A click on a wire selects it, and Delete
cuts it: a wire is thin, and one cut by a click that missed the box beside it
was a wire nobody meant to lose. A right-click on a wire offers Disconnect, and
on a node or a control Delete -- of the selection when that box is in it, else
of the box alone, since a right-click does not select. The browser's menu is the
same, and offers the probes on an output (below).

**The patch's header.** With nothing selected, the parameter panel shows the
file's `name`, `author`, `category` and `description`, what the patch selector
shows of it. A field entered is `NodeEdit::Text::setInfo`, one Undo step; one
emptied takes the statement out, but for the name, which a patch cannot do
without. The category is free text, offered from the list the shipped files
keep to (`src/DspCategories.h`).

**Renaming.** A selected node or control has a Name field at the top of its
parameters, and a control's right-click dialog takes a new name too. The block
and everything reading it are respelt by the lexer's tokens -- `node` and `io`,
every `name->port` and `name[i]->port`, every `@name`, a `controller` block's
`roll = "name"` -- so a comment or any other string that says the name is left
as it is (`NodeEdit::Text::renameNode` and `renameControl`). The box keeps its place, its probes and the selection
(`NodeLayout::Text::rename` carries their lines), in one Undo step. A name
another node or control has is refused, as is one the lexer would not read back
and one element of an array, whose block every element shares. Only this file
changes: a piece that sets `@cut` on the instrument goes on saying `@cut`, and
a patch that is playing keeps the old name until it is loaded again.

The header, the Name and the arithmetic are rows `NodePanel` builds, so the
browser's editor has the same three: `tw_node_propose` says what an edit of a
row means, and the page splices it into the document as it does every other.

**Adding a node.** The palette's Add, a double-click or Enter puts the node in
the middle of the view, beside one already there rather than on it; a plugin
dragged from the palette onto the canvas lands where it is dropped. The
browser's Add node places it the same way (`NodeGraph::placeNear`), and its
toolbar has the desktop's Delete, Arrange and zoom steps too.

**Selecting.** A click selects a box and shows its parameters. A drag on empty
canvas is a rubber band, and dragging any box of a selection moves the whole of
it. Shift with a click adds a box to the selection or takes it out, and Shift
with a band adds what it gathers.

**Keys on the graph.** The arrows go to the nearest box that way, by where the
boxes sit; Shift+arrows move the selection 10 pixels, 1 with Ctrl too, and a
run of presses is one Undo. Delete or Backspace deletes the selection, all but
the io node and expressions, which stay; Escape lets go of it; Ctrl+A selects
every box. Keys wait while a drag is under way, and Ctrl+arrow is left to the
scroller. The canvas takes these in `NodeCanvas::keyPressed`, so the browser's
editor has them too; there a held Shift+arrow is written to the file when the
key comes up, since every write reloads the patch or goes to every peer, and a
rebuild before that drops it rather than writing an old graph's positions over
the new text. Ctrl+S saves on the desktop (Cmd on macOS); in the browser it
says there is nothing to save, and Ctrl+Z undoes the graph rather than the
piece -- in a room, says there is no Undo there yet. A screen reader
hears what a key did -- the box an arrow reached, a group, a wire, a delete, an
Undo: the desktop announces it (GTK 4.14 and up, with an accessibility bus),
and the browser writes it in a live line under the graph, since the page's
status is the patch reload's a moment after every edit. A rebuild keeps the selection by name
(`NodeCanvas::held`), so a peer's edit that renumbers the boxes leaves the same
ones selected. An edit keeps the scroll position too, and an arrow or a nudge
that takes the selection out of sight brings it back into view
(`CanvasContent::showRect`), in both editors.

**Probes.** Right-click an output to watch it with a visual module: a panel
sits above the node, drawing what it produces, and a double-click opens it in
a window of its own. Probes are written into the file's layout block, so a
patch opens with the ones it was saved with. The browser's editor keeps them
the same way, and in a room that makes them the room's: the lines are in the
shared file, so every peer's editor shows them, each drawing from its own
tap. A double-click there draws the panel large in a dialog over the page,
which leaves the graph where it was.

**The graph as a list.** List, the third toggle on the toolbar, shows every
node as a row and, for the chosen one, its wires as buttons: connect,
disconnect, watch and delete, each the canvas's own gesture. It is the
keyboard's and a screen reader's way into the graph. The browser's editor has it
too, as "Nodes and wires, as a list" under the graph.

**When edits reach the file.** On the desktop every edit goes into the
editor's copy of the patch's text as it is made, and reaches disk only on Save — a file rewritten on
every gesture would be alarming. The edits are the splices the browser's editor
makes, `NodeEdit::Text` and `NodeLayout::Text`, so the two agree byte for byte
about what an edit is. Save adds the layout block and writes the text -- into
the piece, for an instrument a piece carries, rather than to a file; Revert
reads it again, as a step Undo can take back. The browser's editor has neither:
an edit there is in the page's text, and so in the patch, as it is made.

**Unsaved means the text is not the file.** Nothing else is held aside, so an
edit undone by hand leaves nothing to save: a value typed back goes in the
file's own spelling, `0.50` staying `0.50`, for that reason. A box dragged back
or a probe disarmed does the same on a file whose layout block already says
where every box is; a file without one gains one at the first drag.

**Undo and Redo** are the arrows by Save, and Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y
(Cmd on macOS) from anywhere in the editor; a text field outside the parameter
panel, such as the palette's filter, keeps Ctrl+Z for its own typing. A step
is one gesture -- a drag, a wire, a node added -- and values typed into one
arg less than a second apart are one step. A step holds the text and where
every box was, since a box nobody has moved has no line in the layout block
and undoing a drag still has to put it back. Opening a file starts the
history over; switching channels, or a patch loaded elsewhere, keeps it.

The solo page's graph has Undo and Redo buttons in its toolbar, and the same
keys anywhere in its pane, list view included; the page's own Undo stays the
piece's. A step there is a `TextHistory` (`wasm/web/history.js`, the piece's
Undo's too) of text alone: the browser keeps every box's place in the layout
block, so the text is all there is to put back. Each gesture runs inside the
view's `step()`, which names it as the desktop does and makes all it writes
one step. There is a history per file and per mode, a patch and a piece's
instrument being able to share a name, and nothing starts one over: a change
made some other way -- the text box, a load -- is taken up as a step of its
own, `change the text`, when the next one is taken.

**A value, a slider, a drag or a probe changes the graph in place; a wire, a
node added or deleted, or a control's range parses the text again.** A node's
ports come from its plugin, and the only thing that knows them is a parse. A
wire can add or remove boxes — cutting the wire out of an expression removes
the expression — and changes which node a control's strip sits on. Every box but the strips, which
go with their nodes, is put back where it was after the parse, so wiring does
not rearrange the graph.

**An edit the text will not take is refused when it is made**, not at Save: a
control the file spells as arithmetic, a number with no spelling in the
grammar. The slider or the panel goes back to what the text says, and the
status bar says why.

**Fit is a button, not a behaviour.** An earlier revision fitted the graph to the
window on opening; that was removed, because shrinking every patch until it fits
makes a wide one unreadable and disguises the fact that it is wide. The
exception is the browser on a phone-width screen, where 1:1 shows one box at a
time and the whole graph small is the only map of it. Fit never
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

**Nearly every control drives exactly one parameter**, and no control anywhere
drives two parameters on the same node. So the ordinary case is a control that
belongs to precisely one box, and it is drawn as a strip stacked on top of that
box — label, track, value on one line, no title bar. A host usually carries one,
and at most a handful.

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

### Expressions are one box

An arg a `.dsp` wrote as arithmetic — `freq = freq->out * exp2(@cents / 1200)`
— is three `math::` nodes by the time the graph is built.
`thSynthTree::desugarExprs` made them, named them `osc2.freq#1`, `#2`, `#3`,
and the audio path knows nothing else. Drawing those three would be showing
the desugar's working rather than the patch, so `NodeGraph` collapses them
back into one box: the arithmetic on a line of its own, an input port per
signal leaf, one output wired to the arg. The box is named `<node>.<arg>`,
which is stable across loads and is therefore what a `# @layout` or `# @probe`
line stores — `thSynth::armProbe` resolves it to the last node of the chain.

**Edited as text.** `canConnect` refuses a wire into one, out of one, and onto
the arg one drives; `NodeEdit::setValue` and `connect` refuse an arg whose
right-hand side is arithmetic, and leave the file untouched. The value in the
file is a graph, and replacing one term of it with a wire is exactly the
re-emission splicing exists to prevent. What changes it is typing: the box's
panel has its Expression, and the arg's row on its node is the same field,
both holding the file's own text rather than the box's folded rendering, so
`exp2(5 / 1200.0)` stays five cents until it is typed over.
`NodeEdit::Text::setExpression` puts what is entered in place of the whole
right-hand side; one written over several lines comes back on its last, the
comments of the lines before kept above it. Text that will not parse is
refused and the field put back, as is anything that could not stand between
the `=` and its `;` -- a `;`, a brace, a string or a comment. Only an arg
already written as arithmetic is offered: a number has its own box and a wire
is drawn. A line an array's elements or the io node's channels share stays
read-only.

Removing the whole thing is cutting its wire: `disconnect` rewrites the arg to
`= 0`, as it does for any other wire.

Deleting a node or a control an expression *reads* is a smaller edit than that,
and used to be a larger one. The reference has to go — left alone it resolves
to nothing and the arg silently reads zero — but nothing around it does, and
replacing the whole right-hand side with `0` took the rest of the author's
arithmetic with it. Deleting `@detune` turned `ladder.dsp`'s `freq = vib->out
+ @detune` into `freq = 0`: the oscillator lost its pitch rather than its
detune, and the edit reported success. So `removeControl` now writes the value
the control held in place of the reference (`freq = vib->out + 1.3`) and
`removeNode` writes `0` in place of its own, since a departed output has no
value to stand for. A plain `a = @cut` still becomes `a = 0`, and so does an
expression reading a control whose value carries a unit — no expression may
hold one — or is arithmetic itself, since then there is no one number to
splice in.

### The few that are shared

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

Measured at the time over the corpus as it was: 18% shorter for exactly the
same width. `LAYER_GAP` then came down from 70 to 44: it was 70 because a
control in layer 0 could have a long wire crossing a column, and they are
strips on their hosts now. On a 17-layer patch that is 440 pixels.

### Retyping one

Right-click a control — its box, or the strip it is drawn as — for its name,
range, label and group. The browser's editor has the same form, there and on
its Add control button; a refusal is said in the form, which stays open. The range is the thing a slider can neither show nor change: a
cutoff declared `0..1` has a slider that reaches a tenth of the filter, and
nothing on the canvas says so or offers to fix it.

Four lines, one edit. `.min`, `.max`, `.label` and `.group` are spliced
together, so a range change is one change to the text rather than four. A line the file does not have is
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
a write of 1 with its six characters intact — the same reason every `th_max`
and `th_min` survives a save anywhere else. Arithmetic is refused
outright rather than folded to whatever it evaluates to today, and refusing the
maximum refuses the minimum with it: half a range this editor cannot read back
is worse than none of it. No shipped `.dsp` spells a range either way — every
range line is a literal — so `dspnew` writes the files that do.

A new name in the same form renames the control, as the panel's Name row
does (`NodeEdit::Text::renameControl`, above): the range goes in first and the
name last, in one write.

Like adding a node, this goes into the text and parses it again at once. A
range decides how the strip is drawn and what the slider can reach, and both
come from a parse — a range that had changed in the text and not on the canvas
would be a slider lying about where its own ends are. Undo takes it back like
any other edit.

### Controls that are not scales

A parameter whose plugin reads it `switch ((int)x)` is a list, not a range.
`waveform` has six cases, so five sixths of a continuous slider's travel picks a
waveform that is already picked, and the shipped patches that drove one used to
declare `.max = 5.1` or `5.5` — padding, so that a slider which could not
reliably land on 5 could still truncate to it.

Where a control is typed — see [ARCHITECTURE.md](ARCHITECTURE.md#arg-type) for
where the type comes from — the strip changes in three ways. The track carries a
tick per value, so it is visibly steppy before it is touched rather than merely
stiff once it is. A drag snaps, in `setControlValue` rather than in the drag
path, because a control that snaps when dragged and not when typed into is worse
than one that never snaps. And it reads `Triangle` rather than `3`, with `4 of
6` where the range would go.

**The drawn range is not always the declared range.** A list of names is its own
range -- not `0..count-1`, but from its first named entry to its last. A `.max`
of 5.1 over six named waveforms is drawn as 0..5; `osc::window` declares six
indices and names three, so it is drawn as 0..3 and its last two fifths do not
exist. A drag snaps to the nearest *named* value for the same reason: a hole in
the list is an index whose case the plugin's switch does not have, and landing
on one selects nothing at all. That is deliberately
*not* done by fixing up `ctlMin`/`ctlMax` when the box is built:
`NodeEdit::setControlMeta` writes those numbers back into the file, so a derived
range there means opening a patch and saving it silently rewrites `.max = 5.1`
to `5` — a change to a line nobody touched. `dspwrite` failed on exactly that,
which is why `Box` carries the declared numbers and `ctlDrawMin`/`ctlDrawMax`
alongside them.

A node added from the palette arrives carrying whatever defaults its plugin
declares, so `osc::simple` comes out saying `amp = 1` and `mul = 1` rather than
saying nothing and leaving the reader to know that an unwritten `amp` is full
scale. `buildArgMap` loads the same numbers into the args the file omits, so
this is for the reader rather than for the sound; `argtype` renders the arg
left out, the default written and a literal `0` written, and compares all
three, to keep that true.

The parameter panel offers a node arg with named values as a dropdown, on the
desktop and in the browser alike, from its first named entry to its last
(`NodePanel`'s CHOICE row); what goes into the `.dsp` is the number.

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

The palette lists every node plugin on disk grouped by category, which is not an
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

It is the same dialog that retypes an existing control, with the value left
out -- a new name there renames it: the two disagree about which fields to show and agree about
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

The editor has to be attached to a channel for this, so Patch graph opens on
the channel picked in Channels, and the status bar says `live on channel 3` or
`not attached to a channel`.

Three kinds of edit, and they behave differently:

- **Controls** are channel args, shared by every note on the channel. Changing
  one is heard immediately, mid-note.
- **A node's own value** is copied into each note at note-on, so changing it
  changes the *next* note and leaves ringing ones alone. The status bar says
  `(next note)` rather than pretending otherwise.
- **Wire changes** are not applied live at all; they need the graph rebuilt.

Undo, Redo and Revert put back what they change the same way:
`NodeHistory::liveChanges` compares the graph before and after by name and
pushes every control and value that differs. What no value can carry -- a
wire, a node added or removed, a plugin, a text arg -- waits for the patch
to be loaded again, and the status bar says so.

## 7. Why the layout will not get much narrower

`ts1.dsp` is an eleven-stage signal chain at 1888 pixels; the deepest in the
corpus is seventeen layers at about 2900, and a good part of the corpus is wider
than 1600. That is what the patches *are*, and three separate levers
were measured and found nearly exhausted.

**The layering is already optimal.** Longest-path puts every node at its
critical-path depth, and no drawing in which every wire goes forwards can have
fewer columns than the critical path. `scripts/dsplayout` checks it: all but
the odd graph are already at the floor. Coffman-Graham and friends bound the number of
nodes *per layer*, which makes a graph taller and can make it deeper — the
opposite of what is wanted here.

**Boxes cannot shrink much either.** A box must fit its longest input and output
port names on the same row. Worst in the corpus is 18 characters together, median
6 to 9, and the header carries the node name plus the plugin name, which already
clips. Perhaps 12% is available, on a 172-pixel layer of which 128 is the box.

**Wrapping works and is off anyway.** `NodeGraph::setWrapWidth` cuts the column
sequence into bands and stacks them, the way a long circuit is drawn on several
rows of a schematic, and it hits the target exactly (measured over the corpus
as it was then):

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

`bd9.dsp` was the widest graph in the corpus and therefore the one that most
needed wrapping — and splitting it in half costs 23 long return wires. These
patches are not chains; they are broad fans, dozens of parallel paths from the
input to the mixer. Wrapping trades a scrollbar for a tangle.

The profile above is kept as the measurement that settled it. `bd9.dsp` was one
of the 2003-04 drawers and has gone; the widest graph now is
`dsp/fx/vocoder.dsp`, a broader fan still -- a band per filter, all in
parallel -- so the argument is if anything stronger than when it was made.

The code stays, defaulted off, and `dspgraph -w 1500` runs every layout invariant
against it so it is not untested code pretending otherwise. If a patch ever turns
up that is deep and *narrowly* connected, it is one call away.

## 8. The checks

The ones marked as gates run in CTest. `argtype` and `exprcheck` build their
own `.dsp` files and need no corpus: the cases that matter — a control two
nodes disagree about, a value list with a hole in it, a `.dsp` overriding its
plugin — are ones no shipped file contains. `dspgraph`, `dspwrite` and `dspab`
run over the corpus. `editorcheck` drives a real editor, so it wants a
display, and skips itself without one. The browser's editor is checked by
`wasm/web/nodecheck.mjs`, which holds every edit the module makes against
`scripts/dspedit`'s bytes, and `pagetest.mjs`, which drives the page; both run
in CI. The rest are run by hand.

| Harness | Gate | Covers |
|---|---|---|
| `argtype` | yes | a plugin's step, value names, description and default; the pass that carries the type to the control driving it, including the disagreement rule in both visiting orders; the `.dsp` override; that no drag and no written value can land on a hole in a value list or off the step; that a node written with its plugin's declared defaults, with zeroes, and with neither all render bit-identically; and that every declared default is what a bare node of its plugin loads holding |
| `exprcheck` | yes | a constant expression still folding to a value and no node, precedence and the unary minus included; an expression rendering bitwise against the nodes it stands for; every box's text re-parsing to the graph behind it; one read-only box per expression with every port attached; `canConnect`, `setValue` and `connect` refusing one and changing no byte; `disconnect` rewriting the whole arg to 0; `removeControl` and `removeNode` replacing the reference and not the arg; a node added beside one and removed again byte for byte |
| `editorcheck` | yes | the editor itself, through its handlers and the canvas's gestures: Save's bytes for every file in the corpus held against the same edits made with `NodeEdit::Text`, in the opposite order, and the positions read back out of what was saved; a saved layout opened as saved and kept through a probe; an edit undone by hand leaving nothing to save; a slider or a value the text will not take refused when it is made; a value set on one element of an array reaching every element; a file that fails to open, or no temporary directory, losing nothing; every kind of edit undone and redone, by button and by key, with what a running synth hears; random Undo and Redo, renames and expression edits among the random edits, each giving back exactly the step's text; the keys and the selection, the header, renaming, arithmetic typed over, the middle button and Space dragging the view, and the view kept across edits |
| `panelcheck` | yes | what `NodePanel` offers for a box and what an edit of each row means: a value, a list's names, the header with nothing selected, a Name, arithmetic in the file's spelling, and each text typed back as it stands asking for nothing |
| `dspprobe` | yes | a probe on every output of every node in the corpus, three voices sounding, reading bit for bit what that node and arg hold when looked up by name |
| `historycheck` | yes | `NodeHistory` with no toolkit: steps back and forth, edits to one arg joined and an Undo ending the join, the cap; what changed in the sound between two graphs, and when that is not all that changed |
| `dspgraph` | yes | every wire on a correctly-facing port, no double fan-in, no overlapping boxes, no `ARG_STATE` exposed, hit-testing on boxes and ports, attached controls against their hosts, shared controls laid out before what they drive, io-node args partitioned across the two halves, probe panels |
| `dspwrite` | yes | values and wires cut and restored across the corpus, byte-identical; every control's range, label and group retyped and restored, and every value clamped by a range narrowed past it; every header field changed and put back; every node and control renamed, the same graph, and back byte for byte; every expression set to the text its box shows, the same graph |
| `dspnew` | no | builds files from nothing: adds and removes one node of every plugin in the catalogue, retypes a control it just added, writes the range spellings no shipped file uses, then renders audio from what it built |
| `dsplayout` | no | layer counts against the critical-path floor, and the wrap-mode cut profile |
| `canvascheck` | yes | the canvas's gestures and keys against a bare `NodeCanvas`, no widget and no display: a click that moves nothing, a drag, a click on nothing, a band, Shift-click, a group dragged, a wire selected and then cut, Delete, Escape, Ctrl+A, the arrows, a nudge, Ctrl+arrow left to the scroller, a pan |
| `dsplive` | no | that moving a control changes the sound of a ringing note |
| `dspab` (the test is `dspab.windows`) | yes | two renders compared bitwise, for changes meant to be inaudible |

Worth repeating about how these are written:

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
- **`dsplive` reports its non-effects separately rather than as errors.** Most
  controls change the sound of a ringing note and some do not — `@a`, `@d` and
  `@r` are envelope times, so by the time the change lands the note is in sustain and
  there is nothing to hear during it. The note is held and never let go, so this
  says nothing about `@r` after note-off.

**Layout quality is subjective and cannot be unit-tested**, and neither can the
feel of a gesture. The bugs that matter here are visual and interactive, and
finding them means sitting in front of it.

## 9. Still ahead

- **A room's graph has no Undo yet.** It wants the file's `Y.UndoManager`,
  shared with the code view, so that an Undo takes back only one's own edits.
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
nothing at all, and `input::alsa` likewise. Wires were being silently dropped. If you are measuring anything over "all args", registered is not the
same set as used.

**A stale header next to a test source.** A crash in a throwaway harness turned
out to be an old `NodeGraph.h` sitting beside the test, so it compiled against
the old `Box` and linked the new one. `#include "NodeGraph.h"` resolves relative
to the including file first.
