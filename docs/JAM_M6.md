# M6 -- the composer view and the node editor

The sixth milestone of [JAM.md](JAM.md): the piece's picture in the
browser, the picture as a control, and the `.dsp` canvas over the shared
document. This document is the detailed plan: what gets built, what it
needs from the engine, what it decides that JAM.md section 3a left open,
and the gates that say it is done.

The one-line shape: **a second scheduler, in a worker, fed the messages
the worklet is fed, holding real composer instances; the desktop's two
canvases split from their widgets and compiled to wasm, drawing through a
cairo stand-in onto the page; a click on a composer's picture is one more
stamped command; and the node editor's model and canvas run over the
room's document.**

M6 depends on M2 and on nothing after it. It runs beside M3 to M5.

The principle that shaped it: **one drawing, one behaviour, two shells.**
Everything that decides what a canvas looks like or does when clicked is
written once, in C++, and runs on the desktop and in the browser. What
differs per platform is the shell around it -- the widget or the DOM
element, the event plumbing, the scroller -- and the shell is thin.

## 0. Where M6 stands

On `jam-m6`, which starts where `game-music` ends:

- **The silent synth** (section 2) is in: `thSynth::setSilent`, notes
  dropped at `addNote`, `process()` applying the queue and nothing
  else, and `droppedCommands()` for whoever watches the ring. `gencheck`
  gates it: every seeded piece composes one tape over a rendering synth
  and a silent one, the piece's instruments land on the silent synth's
  channels, nothing is dropped over the sweep, and the output stays
  zero.
- **The canvas split** (section 6.1) is in: `CanvasContent` in `src/`,
  `NodeCanvas` and `ComposerCanvas` moved beside it with no toolkit in
  them, `GraphCanvas` reduced to the gtk shell base, and
  `NodeCanvasWidget` and `ComposerCanvasWidget` in `src/gui/` as the
  shells. The content classes are three object libraries that link
  cairomm and never gtkmm, which is the guard. All nineteen gates pass,
  composercheck and editorcheck pressing the real widgets; canvasbench
  draws the corpus through the split at the cost it had.
- **The cairo stand-in** (section 3) is in, as `wasm/cairo2d/`: cairo's
  C API and a cairomm face over a recorder, and `replay.js` over a
  Canvas2D. Its own directory, build, README and test, and no include of
  anything in this tree. The browser build compiles the composers' draws
  against it -- `THC_NO_DRAW` is gone -- and `thinkweb` exports the
  chains, the stages, and a stage's picture as the three tables a list
  is. `drawcheck.mjs` gates it: every shipped piece played for four
  seconds, then every picture drawn at 100x100 and at 400x400, each list
  walkable by the arity table alone and known to `replay.js` op for op,
  and every composer that says it draws having drawn something somewhere
  -- 170 pictures from eight composers. `cairo2d`'s own two tests run
  beside it.
- What is not in from section 3: the visual modules. They are section
  7.4's -- nothing loads or draws one until the node editor's probes do,
  and `src/thVisual.cpp` is not in the module yet -- so they come with
  the probe path and its gate rather than ahead of it.
- **The mirror** (section 4) is in as far as it can be without a page:
  `tw_silent` and `tw_step` -- `tw_render` with the render taken out --
  and `engine.js`, the one switch that says what a message means, which
  both the worklet and the mirror go through. `mirrortest.mjs` gates it:
  every seeded piece, both instances in one process, the renderer a
  quantum at a time and the mirror told after every sixteenth how far it
  has got, which is the batch lag the picture will have. One tape,
  nothing dropped, nothing rendered. What is left of section 4 is the
  plumbing: the worker, `host.js`'s second port, and the tape diff on
  the page.
- **The composer canvas** (section 6.2) is compiled into the module and
  drawing: the desktop's class over cairo2d's cairomm face, with the
  shell's four answers in a subclass, and the exports the shared shell
  asks for -- show, draw, press, motion, release, key, zoom, fit,
  extent, dirty. `drawcheck.mjs` draws it over every shipped piece at
  two view sizes. What is left is the page half of the shell: the
  element, the pointer, the replayer, the scroller.
- **Input as a command** (section 5) is in: `tw_input` scheduled beside
  STOP, TEMPO and KNOB and applied at `at` inside the step; `input` in
  `commands.js`, `engine.js` and `host.js`; and `ComposerCanvas::sigInput`,
  which is how a gesture leaves the canvas as a record to be stamped
  rather than a call into the plugin. `mirrortest` runs every clickable
  piece three ways -- untouched, clicked by script, and clicked through
  the canvas in shell pixels -- and holds each clicked tape against the
  untouched one so that a click that changed nothing cannot pass as
  agreement.
- **The mirror's worker and the composer tab** (sections 4 and 6.3) are
  in: `mirror.js`, the worker holding the module with the canvas beside
  the scheduler it draws from; `host.js`'s second port, which is the tee
  -- one function, so "fed the messages the worklet is fed" is a
  property of the code; `canvasview.js`, the page's half of the shared
  shell, which knows nothing about what it is showing and is what the
  node editor's canvas will use; and `composerview.js`, the little that
  is particular to the composer view, on both pages. `tapediff.js` holds
  the two tapes against each other and both pages show the count.
  `pagetest` presses on the solo page's view and `jamtest` paints a Life
  board in a room from one browser and finds the same board in the
  other, and not the tape of the run nobody painted on -- which is the
  first half of gate 8.3.
- **The node editor** (section 7) is in, all but the probes. The writers
  take text and a file is where the text came from (`NodeEdit::Text`,
  `NodeLayout::Text`), the catalogue can be handed its plugins rather
  than finding them, and the model, the canvas and the writer are
  compiled into the module (`thinknode.cpp`). `nodeview.js` is the page:
  the shared shell over the node canvas, a palette and a params panel as
  HTML, and every edit a splice into the room's document. The gates:
  `dspwrite` and `dspgraph` make every corpus edit twice, over a file and
  over its text, and hold the two against each other; `nodecheck.mjs`
  builds every shipped patch through the module, draws the node canvas
  over each, and holds every edit against `scripts/dspedit` -- the same
  NodeEdit natively; `jamtest` types a number into a node in one browser
  and finds it in the other's document and in the desktop's own bytes.
- **The probes** (7.4) are in, and with them the visual modules -- the
  piece of section 3 that was deferred to here. The four join the static
  bundle, renamed like the composers and drawing through the stand-in;
  `tw_probe_arm` is `thSynth::armProbe` on the worklet's one thread and
  the ring is drained after every render, with the samples going to the
  page in the tape batch; the page opens a display per probe in its own
  instance and the canvas draws it in a panel, through the painter slot
  `NodeEditor` fills on the desktop. A right-click on an output port is
  how somebody asks for one. `nodecheck` runs the whole path headless --
  a note played, a thousand samples published, a scope drawing them, the
  canvas drawing the panel -- and `jamtest` arms one in a room.
- **The three gaps are closed**, and the node editor is on the solo page
  too. A wire can be cut from the canvas; a right-click on a port offers
  every visual module this build has and offers to stop when one is
  already watching; and a stage's params handle opens a popover beside
  its box, read-only, filled from the piece the mirror is holding. The
  solo page has no document, so `nodeview.js` takes a file source rather
  than a `Y.Doc`: in a room it is the shared document, and on the solo
  page it is the patch in the text box (an edit reloads it) or one of the
  piece's instruments (heard at the next Load).
- What is left: 8.4, which is two browsers and a person.
- A correction to section 0a, found by the gate: **two** composers take
  input, not three. `ca` and `life` do; `evolve` says in its own header
  that interactive evolution wants the `composer_input` ABI and is
  deliberately not attempted, so `boss` has no picture to click.

## 0a. Where M6 started from

What is in the tree that M6 builds on, and what it works around:

- **The composer ABI already has the three exports M6 needs.**
  `composer_draw` paints with cairo; `composer_input` takes a press, drag
  or release in draw's own coordinates with the `w`, `h` it was drawn at;
  `composer_capture` hands a touched param back as text
  (`libthink/thcomposer.h`). Eight composers draw (`breed`, `ca`,
  `euclid`, `evolve`, `life`, `lsystem`, `markov`, `morph`); two take
  input (`ca`, `life` -- see section 0's correction); one captures
  (`life`). `gencheck` already
  drives `life` and `ca` through `composer_input` with scripted clicks and
  asserts the tape follows, so the ABI half of the M6 gate exists natively.
- **The browser build strips draw and keeps input.** `THC_NO_DRAW` leaves
  `composer_draw` and `<cairo.h>` out of the web module; `input` and
  `capture` are in the static export table (`THINK_COMPOSER_EXPORTS`), so
  the worklet's composers can be clicked today. Nothing on the page sends a
  click, and `thinkweb.cpp` exports nothing about chains or stages: the
  page sees instruments, sinks, knobs and the tape.
- **The two canvases are already most of the way to portable.**
  `ComposerCanvas` (1.6k lines: one row per chain, stages as boxes with
  their draw inside, an enlarged view for a stage whose picture is a
  control) and `NodeCanvas` (1.2k lines: boxes, ports, wires, attached
  controls, probe panels, the rubber band, the pending wire) draw through
  cairomm's toy API alone -- twenty-five distinct methods between them,
  no Pango -- and take their gestures as plain doubles: `onPressed`,
  `onMotion`, `onReleased`, `onKey`, `feedInput`. Selection, dragging,
  hit-testing and the enlarged view all live in the class. What ties them
  to gtkmm is small: `queue_draw`, the zoom and coordinate conversion
  they inherit from `GraphCanvas`, the gesture controllers their
  constructors create, one `grab_focus`, one `get_height`, and a
  `Gdk::Rectangle` in three signals. libsigc++ is already linked into the
  web module, so their signals port as they are.
- **Around the canvases** sit `ComposerWindow` (3.5k lines: the panels,
  the edits through `thcGenEdit`, a 50 ms draw timer), `NodeEditor`
  (2.7k), `NodePalette` and `NodeParams`. Of the composer window's twenty
  distinct calls into the scheduler, only `start`, `stop`, `reset`,
  `setTempo`, `setMuted`, `bindKnob`, `unbindParam` and `injectMidiEvent`
  change anything; the rest read.
- **The synth cannot be stepped without rendering.** Doing so filled the
  1024-deep command ring and dropped commands within one fast-forward
  ([SCHEDULER_PLACEMENT.md](SCHEDULER_PLACEMENT.md) section 4.4).
  `addNote` builds the note's graph on the calling thread and queues it;
  only `process()` retires it.
- **The node editor's model is already GUI-free.** `NodeGraph` (layout,
  hit-testing, connection rules), `NodeEdit` (the text splicer),
  `NodeLayout` (`# @layout` and `# @probe` lines) and `NodeCatalog`
  include only libthink: 4.8k lines that compile anywhere libthink does.
  All three writers take a filename and read and write the file
  themselves; the catalog walks the plugin directory.
- **Probes** are eight slots in `thSynth`, armed by node and arg name on
  the GUI thread, accumulated across voices on the audio thread and
  published a window at a time into a ring the GUI drains. The four
  displays are cairo plugins in `plugins/visual/` (1.5k lines), loaded by
  the editor, not the engine; the web build compiles none of them.
- **The command layer is one function.** `commands.js` makes and applies
  every command the same way on the sender and every receiver, `host.js`
  turns each into a port message, and `worklet.js` applies the message to
  the module. A mirror that receives the same messages is by construction
  fed the same stream.
- **Pieces to gate on.** `life` is in `glider.gen` and `colony.gen`, and
  `colony` is seeded; `ca` is in `loom` and `cavern`, both seeded;
  `evolve` in `boss`, `growth`, `orrery`.

## 1. Decisions M6 makes

JAM.md section 3a left three things open. M6 fixes them, and a few more.

**All drawing runs in wasm, against a cairo stand-in.** The composers'
draws, the visual modules, and the two canvases themselves. Not cairo
compiled to wasm, and not a second drawing of anything in JavaScript.
The whole of what the tree draws with is about thirty-five cairo calls
-- the toy text API, paths, fills and strokes, dashes, clip, save and
restore, the transforms, and one image surface for the spectrogram --
and every one of them maps onto a Canvas2D call. So `wasm/cairo2d/`
holds a `cairo.h` of its own, a cairomm-shaped header over it for the
canvases, and a `cairo_t` that records a display list the page replays.
The one call that needs an answer back, text measurement, gets it from
the browser's own `measureText` through a synchronous import (section
3). Real cairo would bring pixman and freetype for a vocabulary this
small; drawing the canvases again in JavaScript would be two versions of
everything a person looks at.

**Each canvas is split into content and shell.** The content class owns
the drawing, the interaction and the zoom, requests redraws through a
virtual, and emits the signals it emits today. The shell is the gtkmm
widget on the desktop -- controllers, `queue_draw`, the scrolled window
-- and a canvas element with pointer events in the browser. The content
compiles into the web module unchanged (section 6). This is a refactor
the desktop keeps and its checks cover; nothing about how the desktop
looks or behaves changes.

**The mirror is a worker, and it speaks the worklet's protocol.** Not the
main thread. The reasons, in order: the mirror's job is to receive *the
same messages the worklet receives*, and a worker with the worklet's
`receive()` handler gets that by `host.js` posting each message to two
ports; a fast-forward (M4) runs for seconds and must not freeze the page;
and JAM.md's description of the mirror -- another peer that renders
nothing -- is then literally what it is. The composer canvas runs in the
worker beside the scheduler it draws from. The cost is that a picture is
a message away, which section 4 shows is about the desktop's 50 ms timer
anyway.

**A composer click is a command, stamped like a knob.** `input { at,
chain, stage, kind, x, y, w, h, button }`, made with `knobLead`, sent over
the mesh, applied at `at` in the scheduler's step on every peer, the
sender included. The coordinates are the ones draw was handed and `w`,
`h` come along, as the ABI already requires, so every peer inverts the
same arithmetic and reaches the same cell. Stages are named by chain
index and stage index, which is the canvas's own key and is the same on
every peer holding the same document revision. The clicker hears their
own click `knobLead` late, as they hear their own knob; a Life board's
period is half a second and up.

**The composer view reads; the document writes.** The canvas shows
chains, stages, params with their bindings, sinks, mute state and the
pictures, and takes input on the enlarged stage. It does not add, move or
remove stages, set params or mute -- on the desktop those go through
`thcGenEdit` into the work file, and in a room the text in the editor
*is* the piece and every peer sees it change. The canvas already reports
rather than edits (`sigMoveStage`, `sigKnob`, `sigBindKnob`), so in the
browser those signals are simply not connected in M6. Editing the piece
from the canvas is a later step (section 11); M6's gate does not need
it.

**The node editor's model and canvas compile to wasm; the panels are
HTML.** `NodeGraph`, `NodeEdit`, `NodeLayout` and `NodeCatalog` are
correct in ways that took a corpus to get right (the io node as two
boxes, feedback arcs, attached controls, splices that keep every comment)
and `NodeCanvas` is the only thing that knows how to draw and click
them. They go into the module and run in a third instance on the main
thread. What they need first is the seam the desktop should have had
anyway: the writers operating on lines in memory with the file read and
write as thin wrappers, and the catalog fed from a table as an
alternative to a directory walk (section 7). The palette and the params
panel are forms; they are HTML in the browser and gtkmm on the desktop,
and their logic -- which args, what values, why an edit was refused --
is in the model on both.

**An edit on the canvas is an edit to the document, and it is heard at
Apply.** No work copy: the CRDT is the work copy and Apply is Save. A
value set on the canvas becomes a splice into the file's `Y.Text`; the
piece plays it when someone presses Apply (M3) or at the bar (M4). The
desktop's live poke of a playing channel is not carried over in M6.

**Probes come from the worklet, and the displays run on the page.** The
page arms a probe by channel, node and arg through a message; the worklet
calls `thSynth::armProbe` -- the GUI-thread call, on the one thread the
worklet has, which is fine because the ring is only ever drained on that
same thread after `process()` -- reads each armed probe's ring after every
render and posts the samples with the tape batch. The four visual modules
join the static bundle, renamed like the composers, and run in the page's
own instance, drawn by `NodeCanvas::drawProbe` as on the desktop.

## 2. The engine: a synth that never renders

The one libthink change. A `thSynth` in this mode:

- **Drops notes at `addNote`.** Returns true, builds nothing, queues
  nothing. `delNote` and all-notes-off are no-ops. A note that will never
  sound must not copy a tree on the mirror's thread and then sit in a ring
  nobody drains, which is what section 4.4 of SCHEDULER_PLACEMENT.md
  measured.
- **Still applies everything else.** `SET_CHANNEL`, `SET_CHAN_ARG` and
  `loadTree` are real: the scheduler reads chanargs back (`getChanArg`,
  five call sites), resolves instruments against loaded trees, and applies
  instrument values at load and rewind. So a mirror step calls `process()`
  as the worklet does, and `process()` in this mode drains the command
  ring, collects retired objects, and skips the DSP.
- **Is set once, at construction or before the first load.** Not a toggle
  a running synth flips.

Gate, native, in `gencheck`: every seeded piece composed twice, over a
rendering synth and over a silent one, gives one tape. Small, and it is
what makes the mirror an instance rather than a redesign.

## 3. The cairo stand-in

`wasm/cairo2d/`: `cairo.h`, `cairomm/context.h`, the recorder, the
replayer, a README and a test. Built into the web module only.

**The surface.** `cairo_t` is a display list under construction. The C
functions the composers and visuals call, and the `Cairo::Context`
methods the canvases call, append ops:

- path: `move_to`, `line_to`, `curve_to`, `arc`, `rectangle`,
  `close_path`, `begin_new_path`, `begin_new_sub_path`;
- paint: `set_source_rgb`, `set_source_rgba`, `set_line_width`,
  `set_line_cap`, `set_dash`, `unset_dash`, `fill`, `fill_preserve`,
  `stroke`, `paint`, `clip`;
- state: `save`, `restore`, `translate`, `scale`;
- text: `select_font_face`, `set_font_size`, `show_text`,
  `get_text_extents`;
- for the spectrogram, an image surface: `image_surface_create`,
  `get_data`, `get_stride`, `mark_dirty`, `set_source_surface`,
  `pattern_set_filter`, `surface_destroy`, whose pixels stay in the heap
  and are blitted by reference.

Anything else is a link error, deliberately: a canvas or composer that
grows a new call fails the web build in the header it added, exactly as
`thinkstatic.h` does for a new include. The cairomm header is the thin
C++ face the canvases already use -- `Cairo::RefPtr<Cairo::Context>`,
the methods above, `cobj()` for handing the C pointer to a plugin's draw
-- over the same list.

**Text measurement.** `get_text_extents` is the one call that needs an
answer while the list is being built: both canvases use it to centre
titles and truncate labels. The stand-in answers it through an
Emscripten JS import that calls `measureText` on an offscreen 2D context
with the current font, synchronously. That works in a worker and on the
main thread, and it means a label is laid out from the metrics of the
font it is then drawn with, which is the property the desktop has.

**The encoding.** One flat `float` array per draw: an opcode, its
operands, next. Strings go in a side table by index. The page reads the
array out of `HEAPF32` after the draw returns and replays it on a
`CanvasRenderingContext2D`, whose vocabulary is the same one: `moveTo`,
`lineTo`, `bezierCurveTo`, `arc`, `rect`, `fill`, `stroke`, `clip`,
`setLineDash`, `fillText`, `save`, `restore`, `translate`, `scale`,
`drawImage` from an `ImageData`. The toy font "sans" becomes
`sans-serif`; the device pixel ratio is a scale the replayer applies
first. The replayer is one function, about 150 lines, shared by every
canvas and every picture on the page. A Life board at 60×60 is 3,600
rectangles a frame; Canvas2D draws that many in well under a
millisecond.

**Its own directory, and nobody else's includes.** Nothing quite like
this exists to borrow: real cairo has been compiled to wasm more than
once, and there are C bindings *to* the Canvas API, but no library
presents cairo's own API to C code and replays it on a Canvas2D. So the
stand-in is laid out from the first commit as something that could
leave: `wasm/cairo2d/` with the C header, the cairomm face, the recorder,
the replayer and its own README and test, and not one include of
anything in this tree. thinksynth uses it the way anyone would, by
putting it on the include path. When it has a second user it is a
`git subtree split` and a repository of its own; until then it lives
here, where its only user can keep it honest.

**Gates**, in a Node test over the module: every drawing stage of every
seeded piece produces a non-empty list of known ops at 100×100 and at
400×400; each canvas drawn over every seeded piece and every shipped
`.dsp` produces a list of known ops; and a mirror that draws its canvas
after every step delivers the same tape as one that never draws. The
last is the one that matters: a draw that touches state -- a lazily built
cache, a counter -- is a mirror whose picture is right and whose tape
drifts, and this is the only place it would show.

## 4. The mirror

**The shared handler.** `worklet.js`'s `receive()` -- the switch that
turns a `load`, `instrument`, `chanarg`, `piece`, `transport`, `begin`,
`at`, `knob`, `midion`, `midioff`, `on`, `off`, `alloff` message into a
`tw_` call -- moves into a module both the worklet and the mirror import.
The worklet keeps `process()` and the tape batching; the mirror gets a
`step` in their place, and the composer canvas.

**The tee.** `host.js`'s `Synth` grows a second port. Every message it
posts to the worklet it posts to the mirror. Two additions cross only to
the mirror: `align`, carrying the frame the worklet aligned at (the value
it passed to `tw_align`, which the worklet reports once in its first tape
batch), so the mirror's frame numbering is the worklet's; and `step`,
carrying the frame the worklet's last tape batch reached. The mirror
steps to that frame: applies the commands due, steps the transport to the
end time, drains the silent synth, drains its own tape. That is
`tw_render` without the render -- `tw_step(frames)`, in `thinkweb.cpp`,
sharing `applyDue` with it.

**The lag.** A tape batch is every 16 quanta: 43 ms at 48 kHz, plus a
worker hop. The mirror's picture is that far behind the ear. The desktop
draws on a 50 ms timer and nobody has noticed. If a smoother picture is
ever wanted the mirror can step to an estimate of the worklet's frame
between batches, from the audio clock `clock.js` already fits; the tape
gate does not care, because a step boundary does not change a tape.

**The tape diff.** The mirror posts its tape as the worklet does. The
page holds the two against each other, event for event, per epoch, and
shows the count of disagreements beside `late`. Zero, continuously, is a
determinism check the jam gets for free; non-zero is a red number and,
in M6, nothing more -- a resync (reload the mirror from the document and
fast-forward) is M4's machinery.

**The instance.** The same module bytes the worklet was handed, posted to
the worker and instantiated there, so there is one path for getting a
module into a thread and no `WebAssembly.Module` on a port (host.js says
why). `-sENVIRONMENT` gains `worker`. Memory is a second heap of the same
initial size; a third comes with section 7.

**What the mirror exports**, beside the canvas calls of section 6:

```
tw_chain_count()  tw_chain_name(c)  tw_chain_input_midi(c)  tw_chain_muted(c)
tw_stage_count(c) tw_stage_name(c, s)  tw_stage_plugin(c, s)
tw_stage_param_count(c, s)  tw_stage_param_name/value/text/units/knob(c, s, p)
tw_chain_sink_count(c)  tw_chain_sink_channel(c, k)
tw_pending_count()  tw_capture(c, s, p)
```

The param exports feed the HTML popover the canvas asks for through
`sigParams`; everything else the canvas reads itself from the scheduler
it shares a heap with.

## 5. Composer input as a command

**On the wire.** `commands.js` gains `Maker.input(chain, stage, kind, x,
y, w, h, button)`, stamped with `knobLead`, and `apply()` gains the case:
`synth.input(...)`, which `host.js` posts as an `input` message. Dedupe,
gaps and lateness are the ones every command has.

**In the module.** `tw_input(at, chain, stage, kind, x, y, w, h, button)`
schedules an `INPUT` beside `STOP`, `TEMPO` and `KNOB` in the ordered
list `thinkweb.cpp` keeps, and `applyScheduled` builds a `thcInputEvent`
and calls the stage plugin's `input` on its instance -- at `at`, inside
the step, before any stage ticks at or after it, which is the property
M3 gave every scheduler-facing command. A stamp below zero is "now", as
for a knob on a stopped transport. Both the worklet and the mirror apply
it: the worklet's is what sounds, the mirror's is what is drawn. A chain
or stage index that names nothing is dropped and logged, never applied
to a neighbour.

**Where the click comes from.** On the desktop `ComposerCanvas::feedInput`
finds the enlarged stage, checks the click is inside its rectangle,
converts to draw coordinates and calls the plugin. In the browser the
same method runs in the worker but, instead of calling the plugin, hands
the converted event to a hook the shell sets, and the shell posts it to
the page as a command to make. So the geometry -- which stage, is it
inside, what are `x, y, w, h` -- is decided by the one piece of code that
draws the rectangle, on every platform, and the only thing the browser
adds is that the plugin hears about it at `at` rather than at once.

**Drags.** A press, the drags, a release: each is a command. A pointer
moves faster than a mesh wants; the page sends at most one drag per
animation frame, which is the rate a knob's slider already produces.

**What the desktop does differently.** There, a click lands between two
ticks whenever the GUI thread gets to it, and the tape depends on the
wall clock of the click. In a room it lands at `at` on every peer, which
is the stronger promise and the one the gate holds.

## 6. The canvases, split

### 6.1 The split

Each of `NodeCanvas` and `ComposerCanvas` becomes two classes:

- **The content class**, keeping the name, with no gtkmm in it. It owns
  the zoom and the widget-to-content conversion that `GraphCanvas` holds
  today, its selection, drag and enlarged state, every `draw*` method
  taking a `Cairo::RefPtr<Cairo::Context>`, every `on*` handler taking
  doubles, and the signals. Where it called `queue_draw` it calls a
  virtual `requestRedraw()`; where it called `contentResized` and
  `grab_focus` and `get_height`, three more virtuals. `Gdk::Rectangle`
  in the three signals becomes a plain rectangle struct.
- **The widget**, a subclass of the content class and of `GraphCanvas`,
  which keeps the scrolled-window half: it creates the controllers in its
  constructor and forwards to the `on*` handlers as the constructors do
  now, implements the virtuals with `queue_draw` and friends, and hands
  `on_draw`'s context to the content's draw. It is a few dozen lines
  each.

Nothing the desktop does changes. `composercheck`, `editorcheck` and
`canvasbench` cover the content classes before and after, and they are
where the widget gets pressed.

### 6.2 In the browser

The content classes compile into the module. `ComposerCanvas` is
instantiated in the mirror worker, over the scheduler there and the
piece as `thcGenEdit::describe` reads it from the file the loader already
wrote. `NodeCanvas` is instantiated in the page's own instance, over the
`NodeGraph` built there (section 7). Each gets a shell in JavaScript,
and the shell is the same for both:

```
tw_canvas_draw(canvas, w, h)       -> a display list (section 3)
tw_canvas_press/motion/release(canvas, x, y, button, modifiers)
tw_canvas_key(canvas, keyval)
tw_canvas_zoom / set_zoom / zoom_to_fit / extent
tw_canvas_signal(canvas)           -> the signals fired since last asked,
                                      as records the shell dispatches
```

The shell owns a `<canvas>` element inside a scrolling `<div>`, converts
pointer events to content coordinates with the zoom the content class
reports, calls the handlers, and redraws on the next animation frame
when the content asked for one -- `requestRedraw` sets a flag the shell
reads. For the composer canvas the shell is split across the worker and
the page: the worker half holds the instance and draws, the page half
holds the element and the pointer, and messages carry events one way and
lists the other.

**What is JavaScript, and why.** The replayer; the pointer, keyboard and
scroll plumbing; and the forms: the composer's param popover (which
`sigParams` asks for, with a rectangle to put it beside), the node
palette, and the node params panel. The forms are lists and inputs whose
contents come from the model, and they are gtkmm on the desktop for the
same reason they are HTML here: a form is the platform's. Everything
that draws a box, routes a wire, decides what was clicked or what a drag
means is the one C++ that the desktop runs.

### 6.3 The composer tab

A tab on the room page beside the editor and the roll, and on the solo
page too, since the mirror needs no relay. It is `ComposerCanvas` as the
desktop shows it: one row per chain, name, stages, sinks in their
channel's colour; a stage's picture inside its box; a handle that asks
for the params popover; double-click to enlarge a stage that takes
input; press, drag and release on the enlarged picture become commands
(section 5); Escape puts it back. The canvas is redrawn in the worker
once per animation frame while its tab is visible and not at all
otherwise.

## 7. The node editor

The larger piece, though smaller than it was before the split. Three
parts: what the model needs before it can be shared, the exports, and the
page.

### 7.1 Native refactors, gated by the existing checks

- **`NodeEdit` and `NodeLayout` over text.** Every operation today reads
  the file into lines, edits, and writes the lines back. The line-level
  operations become the API -- lines in, lines out, a `Result` -- and the
  filename overloads become wrappers that read, call and write.
  `editorcheck` covers the file path; the same fixtures run through the
  text path. `NodeLayout::read`, `readProbes` and `write` get the same
  treatment.
- **`NodeCatalog` from a table.** Beside `scan(path)`, a way to be given
  the entries: name, category, and a loader for ports and description. The
  desktop keeps walking the directory; the browser hands it the static
  table, and ports come from the same `module_init` the parser runs.
- **A parse from text.** `thSynth::parseTree` takes a filename;
  `tw_load` already writes the text into the module's file system and
  loads from there, and the editor's instance does the same. No change,
  noted so nobody looks for one.
- **The canvas split** of section 6.1.

### 7.2 Exports, in the page's own instance of the module

The graph and its canvas:

```
tw_graph_build(text)  tw_graph_layout()  tw_graph_apply_layout(text)
tw_graph_box_count() / box name, kind, ports   (for the params panel)
tw_canvas_* over the NodeCanvas instance          (section 6.2)
```

The edits, each taking the file's text and returning the new text or a
refusal with its sentence:

```
tw_edit_set_value  tw_edit_connect  tw_edit_connect_control  tw_edit_disconnect
tw_edit_set_chanarg  tw_edit_add_node  tw_edit_remove_node
tw_edit_add_control  tw_edit_set_control_meta  tw_edit_remove_control
tw_layout_write(text)  tw_layout_read_probes(text)
tw_catalog_count / name / category / description / ports
```

The canvas's signals -- box moved, selected, connect, disconnect,
refused, control changed, context requested, probe activated -- come out
through `tw_canvas_signal` and the page does with each what `NodeEditor`
does on the desktop: an edit, a rebuild, a status line.

### 7.3 The page

- **The canvas** is the shared shell of section 6.2 over `NodeCanvas`.
  Zoom with Ctrl+wheel, fit as a button.
- **The palette** is an HTML list from the catalog, by category; adding a
  node is an edit followed by a rebuild, as on the desktop. **The params
  panel** is HTML inputs over the selected box's args; a change is an
  edit.
- **Edits reach the document as splices.** The new text against the old:
  common prefix, common suffix, one delete and one insert in the middle,
  inside a `Y.Doc` transaction against the `Y.Text` as it stands at that
  moment. `NodeEdit` changes a line or two, so the splice is small and
  local. Positions go the same way on drag end.
- **Which file.** The tab shows one `.dsp` from the document's file map;
  the instrument channel that loads it, if any, is the channel probes are
  armed on.

### 7.4 Probes

- Page to worklet: `probe { slot, channel, node, arg }` and `unprobe {
  slot }`; `tw_probe_arm(channel, node, arg)` calls `thSynth::armProbe`,
  and a disarm the reverse. The worklet reads each armed ring after every
  render and posts the samples with the tape batch: eight probes at 2048
  samples of `float32` is 64 KB a batch, a megabyte and a half a second
  at the very most, and nothing when none is armed.
- The visual modules compile into the module under renamed exports like
  the composers (`think_add_visual` stops being empty). The page opens an
  instance per probe in its own wasm instance and feeds it the samples;
  `NodeCanvas::drawProbe` draws it into the panel through the painter
  slot it already has, which on the desktop `NodeEditor` fills and here
  the page does. The spectrogram's image surface is the reason section 3
  carries one.
- `# @probe` lines are read and written by `NodeLayout` as on the desktop,
  so a probe travels with the patch.

## 8. Gates

### 8.1 Native

- `gencheck`: rendering synth and silent synth, one tape, every seeded
  piece (section 2).
- `editorcheck`: the text overloads produce what the file overloads
  produce, every fixture.
- `composercheck`, `editorcheck`, `canvasbench`: unchanged and passing
  across the canvas split.

### 8.2 The module, under Node

`wasm/web/mirrortest.mjs`: the browser module twice in one process, one
instance stepped as the worklet is (rendering, at 256 and 48 kHz) and one
as the mirror is (silent, stepped to the first's frames from batches of
16 quanta), both fed the same messages, including `input` commands at
stamped times on `colony`, `loom` and `boss`. One tape, and it is the
tape `genwav.mjs -c` delivers under the same commands with the clicks
scripted the way `gencheck` scripts them. The mirror drawing its canvas
after every step against never drawing: one tape. Every drawing stage's
list and every canvas's list non-empty and every op known, over every
seeded piece and every shipped `.dsp`. A press on the enlarged Life
stage through `tw_canvas_press` comes out as an input event whose
`x, y, w, h` name the cell `gencheck`'s arithmetic names. In the CI `wasm`
job.

### 8.3 Two headless browsers

Extending `jamtest.mjs`. Two pages in one room on `colony.gen`. Play from
one; on the other, open the composer tab, enlarge the Life stage and click
three cells; the tapes agree with each other, and *differ* from the
unclicked run -- the `hands.gen` lesson: a click that changed nothing
would look like agreement. Then the node editor: on one page open the
piece's first instrument on the canvas, set one value and connect one
wire; the other page's document shows the same text, and it equals what
native `NodeEdit` writes for the same two operations on the same file;
Apply on either; one tape. Arm a scope on the instrument and assert the
worklet posts samples for it. In the CI `wasm` job.

### 8.4 By hand

Chrome and Firefox: the composer tab and the node canvas look like the
desktop's for every shipped piece and patch, since they are the same
code; a drag paints a line of cells; the spectrogram scrolls; the
composer tab in a background window costs nothing.

## 9. Order of work

1. **The silent synth** (2), with its gate in `gencheck`. An afternoon,
   and it decides whether the rest is an instance or a redesign.
2. **The canvas split** (6.1), native, gated by the checks that exist.
   Independent of everything else and worth doing first because it is
   what makes the rest small.
3. **The cairo stand-in** (3), `THC_NO_DRAW` off the web build, the visual
   modules into the bundle, the draw gates.
4. **The mirror** (4) with the composer canvas in it, the shared shell
   (6.2), the tape diff. Gate 8.2 without the clicks.
5. **Input as a command** (5), the composer tab (6.3), gate 8.2 with the
   clicks and the first half of 8.3.
6. **The node editor** (7): the text seams, the exports, the page over
   the shared shell, probes. The second half of 8.3.

Steps 1 and 2 share nothing; step 6 needs 2 and 3 and nothing after. Two
branches at once is natural: the engine and the mirror on one, the
canvas split and the node editor on the other.

## 10. Size

Against M3, which was the relay, the mesh, the clocks, the room page and
three harnesses:

| | | |
|---|---|---|
| silent synth | libthink, ~100 lines, one gate | S |
| canvas split | two canvases, `GraphCanvas`, ~300 lines moved and ~100 new, no behaviour change | S |
| cairo stand-in | shim ~700 lines with the cairomm face, text measurement and the image surface; build changes | S–M |
| mirror | `host.js`/`worklet.js` refactor, worker, `thinkweb.cpp` exports ~400 lines, the shared shell ~300 lines of page, harness | M |
| input command, composer tab | `commands.js`, `thinkweb.cpp`, the popover, ~300 lines of page | S |
| node editor | text seams ~300, exports ~400, palette and params ~400 lines of page, probes ~300 | M |

The whole is about an M3. Before the split it was two; what went was the
2,800 lines of JavaScript that would have drawn and clicked what the C++
already draws and clicks.

## 11. Not in M6

- **Editing the piece from the composer canvas** -- adding, moving,
  removing stages, setting params, muting. The canvas already emits the
  signals; what is missing is `thcGenEdit` over text, the same seam
  `NodeEdit` gets in 7.1, and a decision about what an edit means in a
  room. That is the step after this one, once the view has been looked
  at.
- **Capture into the document.** `tw_capture` is exported and the button
  is one splice, but a captured board is a document edit whose apply
  semantics are M4's; under M3 it restarts the piece. It follows the
  previous item.
- **Live preview of a node-arg edit** without Apply. The desktop pokes a
  playing channel; in a room that would be one more stamped command per
  edit, and it waits until apply-at-bar says what an edit is.
- **A resync when the mirror's tape disagrees.** Shown, not repaired; the
  repair is a fast-forward, which is M4.
- **The enlarged probe window**, mobile gestures on the canvases, and the
  composer view for a spectator, which is JAM_BACKLOG.md section 2's.

## 12. Risks particular to M6

1. **A draw that touches state.** The mirror's picture stays right and
   its tape drifts, and only the draw-versus-no-draw gate in 8.2 sees it.
   Run it on every drawing composer, and on every new one.
2. **Text metrics.** The canvases lay labels out from `get_text_extents`;
   in the browser that is `measureText` on the font the replayer will
   use. If the import ever measured a different font than the replay
   drew with, titles would be off-centre and truncation wrong -- visible
   at once, and the fix is the one font string in one place. A width
   table would have been the same risk without the browser's help.
3. **Input coordinates.** Every peer applies the same `x, y, w, h`, so a
   plugin whose click mapping is a pure function of them lands on the
   same cell everywhere. `gencheck` computes `life`'s cells by arithmetic
   for exactly this reason; `ca` and `evolve` are the same shape. The
   conversion from a pointer to those four numbers is `feedInput`'s, on
   every platform, so it cannot differ between them.
4. **A late input.** Applied at once and counted, as a late knob is; the
   tapes part from that click on. M3's hazard, M4's recovery.
5. **The splice against a moving document.** Two peers editing one
   `.dsp` at once, one on the canvas and one in the text. The CRDT merges
   at the character; a splice computed from a stale text is the risk, and
   computing it inside the transaction against the text as it stands
   closes it. The remaining case, both editing the same line, is the
   same conflict two text editors have and the CRDT resolves it the same
   way.
6. **Three instances of a 600 KB module.** Compile once, instantiate
   three times; three heaps. Measure the page's memory on a phone before
   calling the solo page done.
7. **A picture a hop away.** The composer canvas lives in the worker, so
   a press goes to the worker and its redraw comes back: a few
   milliseconds on top of the batch lag of section 4. The node canvas is
   on the main thread and redraws through wasm during a drag, at a
   median of thirteen boxes; cheap, but measure a drag on the widest
   patch (`ts1`) before calling it done.
8. **The split drifting.** Two shells over one content class is only a
   saving while nothing platform-specific creeps into the content. The
   web build is the guard: the content classes compile with no gtkmm on
   the include path, so the first `Gtk::` that gets in fails the `wasm`
   job.
9. **The visual modules' FFT** under wasm. Pure C, no libm surprises
   expected; `visualcheck` has no wasm twin, so the display gate in 8.2 is
   the only one, and it only checks that something was drawn.
