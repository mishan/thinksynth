# Scoping live visualizers in the node editor

Measured against the 92 shipped `.dsp` files and the 62 built plugins, on
`node-visualizers` off `patch-selector`.

The ask: while a sound is playing, be able to see the signal at a chosen point
in the graph — a scope, a spectrum, a spectrogram, a level meter — so that
"the filter is wrong" can be looked at instead of guessed at.

The headline: **the engine already makes this cheap, for a reason nobody was
aiming at.** Node ids and arg indices survive the per-note tree copy unchanged,
so summing one node's output across every sounding voice is an O(1) lookup per
voice and a `windowlen` add loop — no search, no allocation, nothing that has
any business being called expensive. The work is almost entirely in the plugin
ABI, the canvas, and the tests.

## 1. What a probe is, and what it is not

Two things could be called a "visualizer node" and they are very different
amounts of work:

- **A pass-through DSP node** — `analysis::scope` wired in-line, `out = in`,
  written into the `.dsp`. It is a node in the file, it survives outside the
  editor, and it is what the phrase most naturally means.
- **A probe** — a GUI-side attachment to an existing output port. It does not
  appear in the signal path, it cannot alter the sound, and inserting one costs
  no rewiring and no reload.

**This proposes the probe.** The reasons are specific, not aesthetic:

*Inserting a pass-through node is not a cheap gesture in this editor.* §8 of
`NODE_EDITOR.md` records that wire changes reach the file only on Save and are
never applied live, because they need the graph rebuilt. So "put a scope on the
filter output" would mean: add a node, cut a wire, lay two more, save, reload
the channel, lose every ringing note. That is a poor experience for something
whose entire purpose is to be reached for mid-tweak and dropped a moment later.

*A pass-through node cannot be trusted not to change what it measures.* It
becomes a `thNode` in the graph, it participates in `setActiveNodes()` recalc
propagation, and it gets copied into every voice. A debugging aid that can
perturb the thing being debugged is worth less than one that cannot.

*The plugin layer has no per-node shared storage.* A `.so` is dlopened once and
every node using it shares the file-static `args[]` array; per-instance state
lives in `ARG_STATE` args, which are copied per voice. A tap plugin would
therefore need a side-channel keyed by node name to find "its" ring — inventing
exactly the mechanism the probe design has anyway, and adding a node to the
graph on top.

**What is a plugin is the display.** See §4: `visual/scope.so`,
`visual/spectrum.so`, `visual/spectrogram.so`, `visual/meter.so`, a new
category with its own ABI, loaded by the GUI rather than by the parser. Adding
a new kind of visualizer means dropping a `.so` in, exactly as adding a new
oscillator does.

The tap itself has to be in `libthink`: it runs inside the audio callback, next
to the mix loop. That split — engine owns the tap, plugins own the drawing — is
the one thing here worth disagreeing with early rather than late.

## 2. Why the tap is cheap: ids survive the copy

A note is a whole copy of the channel's prototype tree (`thMidiNote::thMidiNote`,
`thMidiNote.cpp:27`, via `thSynthTree`'s copy constructor). So "the filter's
output" is not one buffer; with eight voices sounding it is eight buffers in
eight separate trees. Summing them is what "the waveform flowing through that
filter" means, and the obvious fear is that finding them costs a name lookup
per voice per window.

It does not. `thNode`'s copy constructor carries the id across —

```c
    id_ = copyNode.id();            /* thNode.cpp, copy ctor */
```

— and `copyHelper` inserts with `newNode(newnode, false)`, which is the
overload that does *not* reassign one (`thSynthTree.cpp`). Arg indices come
from the plugin's registration order and `copyArgs` preserves them. So a probe
is a pair of integers,

```c
    struct thProbePoint { int nodeId; int argIndex; };
```

and resolving it in any voice's tree is

```c
    thNode *n = tree->nodeAt(p.nodeId);      /* bounds-checked, may be NULL */
    thArg  *a = n ? n->getArg(p.argIndex) : NULL;
```

two array subscripts. `nodeAt()` returning NULL is a real case and not an error:
the copy constructor only walks nodes reachable from the ionode, so an
unreachable node leaves a hole. The probe skips that voice.

Cost, worst case in the corpus: `polymax_` defaults to 10 (`thMidiChan.cpp:38`),
the mean patch has 14 nodes, and a window is 1024 samples. Four probes across
ten voices is 40 lookups and 40k float adds per window, against 44100 windows'
worth of samples per second — call it 1.7M adds a second, on a thread that is
already running dozens of plugin callbacks over the same data. It will not show
up in a profile.

## 3. The tap

### Where it goes

`thMidiChan::process()` already iterates every sounding note, calls
`data->process(windowlength_)`, and then reads `out0`/`out1` off that note's
tree to mix it (`thMidiChan.cpp:416-468` for held notes, `:471-516` for
decaying ones). The probe accumulate goes in the same two loops, immediately
after `data->process()` — the point at which that voice's every arg holds this
window's samples and nothing has been freed.

Both loops, not one. A note in release is still sounding, and a scope that went
blank the instant a key came up would be wrong in the most visible way possible.

### Arming and disarming

Same discipline as everything else that crosses the thread boundary
(`thSynthCommand.h`): the GUI thread allocates, the audio thread installs.

```c
    enum Type { ..., SET_PROBE };   /* install `probe' in slot `probeSlot' */
```

A `thProbe` carries the channel, the `thProbePoint`, and an owned
`thSampleRing`. `applyCommand` swaps it into a fixed-size `probes_[]` array on
the audio thread and hands the displaced one back through `retired_` — a fourth
`thRetired::Kind`. Nothing new is invented; the arming path is the shape of
`SET_CHAN_ARG`.

A fixed eight slots. Probes are a debugging aid, not a mixer, and a bounded
array means the audio thread's inner loop is a `for` over a small fixed count
with no container to walk.

### The ring

`thRing<T, CAPACITY>` is a slot-per-item SPSC queue, which is the wrong shape
for audio: pushing 1024 floats one at a time means 1024 release stores. A
sibling, `thSampleRing`, does bulk `memcpy` in and out with the same
head/tail/release-acquire argument — one release store per window rather than
per sample.

Overrun policy: **drop the newest window and count it.** The audio thread
cannot block and it cannot advance the consumer's tail, so the choices are drop
the new data or corrupt the old. Dropping is invisible on a scope and the count
is what tells the GUI to say so rather than silently lie about the signal. Size
it at 8× the window (8192 floats, 32KB) — a 60fps GUI drains 735 samples a
frame, so eight windows is roughly ten frames of slack.

### Non-audio-rate args

79 of the 126 `allocate()` calls across the plugins ask for `windowlen`. The
rest ask for 1, 2, 3, or a count of their own — filter history, oscillator
tables, `env::adsr`'s position. `thArg::operator[]` already folds a length-1 arg
to a constant, so the tap reads through it and a scalar arg probes as a flat
line at its value, which is the honest picture. `ARG_STATE` args are not
probeable at all, for the same reason `NodeGraph` refuses to wire them.

### Invalidation

A probe is `(nodeId, argIndex)` against *the tree the channel is playing right
now*. Two things break that, and both must be handled or the probe reads a
different node's buffer without saying so:

- **A patch load.** `SET_CHANNEL` replaces the channel; every probe on it is
  disarmed by `applyCommand` at the same instant, and the GUI re-resolves by
  name and re-arms.
- **An edit that adds or removes a node.** Ids are assigned in parse order, so
  deleting a node shifts every later one. The editor already reloads on
  add/delete (`NODE_EDITOR.md` §11), so this rides on the same path.

Re-resolution is by **node name and arg name**, which is what the `.dsp`
actually says and the only thing stable across a reparse.

## 4. The visual plugin ABI

A new category, `visual/`, with its own interface version, loaded by the GUI.
Not the DSP ABI: `module_callback(thNode*, thSynthTree*, ...)` has nothing to
say about drawing, and overloading it would make every visualizer pretend to be
a graph node.

```c
#define VISUAL_IFACE_VER 1

extern "C" {
    /* once per .so: name, description, preferred size, declared options */
    int   visual_init    (thVisual *v);

    /* per probe instance; the host keeps the pointer opaque */
    void *visual_open    (thVisual *v, unsigned int samplerate);
    void  visual_close   (void *inst);

    /* new samples, whenever they arrive */
    int   visual_feed    (void *inst, const float *samples, unsigned int n);

    /* draw the current state; w and h in device pixels */
    int   visual_draw    (void *inst, cairo_t *cr, int w, int h);

    void  visual_cleanup (thVisual *v);
}
```

Both `feed` and `draw` run on the GUI thread — the tap ring is drained there —
so a visual plugin may allocate, hold history, and use whatever it likes. None
of the RT discipline that governs `plugins/osc` applies here, and saying so
explicitly is what keeps someone from carrying it over out of habit.

### Feed and draw are separate on purpose

A spectrogram must see every sample to produce a continuous waterfall, but it
is drawn 30 times a second. Folding the two together would either drop columns
between frames or force the drawing rate to the audio rate. Splitting them
means `feed` runs on every drain and `draw` runs on the frame tick, and the
plugin decides what accumulating means for it. The meter integrates, the scope
keeps a triggered slice, the spectrogram appends columns.

### The drawing target

`cairo_t *` — and this is the one dependency decision in the proposal.

The alternatives were a raw RGB pixel buffer and a data model (curve, columns,
scalar) drawn by the canvas. Both avoid a new dependency in the plugin layer;
both also mean a visual plugin cannot invent a look the canvas did not
anticipate, which defeats the point of making these plugins at all. A
spectrogram wants a pixel buffer, a scope wants crisp lines, a meter wants
text — nothing short of a real drawing API covers all three.

Cairo is a stable C ABI, and it is already present on every platform this
builds for: `gtkmm-4.0` pulls it in, and `cmake/GtkRuntime.cmake` already
reasons about which cairo surface backend is in play on macOS and Windows. The
containment is that only `visual/` links it — `libthink` and the 62 DSP plugins
are untouched, and `cmake/ThinkPlugin.cmake` grows a second macro rather than a
changed one.

It also buys the headless test in §6 for nothing: an image surface needs no
display, so every visual plugin is testable in CI on all three runners.

### The four

| plugin | state | cost |
|---|---|---|
| `visual/meter` | peak and RMS with a decay | trivial |
| `visual/scope` | a triggered slice, hold, time base | small |
| `visual/spectrum` | one FFT per frame, windowed, log magnitude | needs an FFT |
| `visual/spectrogram` | FFT per hop plus a scrolling column buffer | the expensive one |

`meter` first, deliberately, even though it is the least interesting to look
at: it is the smallest thing that exercises open/feed/draw/close, and getting
the ABI wrong is cheaper to discover against forty lines than against a
waterfall. Given the gain-staging work in `REVIVAL.md` §6 it is also a number
worth having at an arbitrary point in the graph rather than only at the master
output.

**On the FFT.** There is no FFT in the tree. `plugins/fft/dsp.c` exists, is 304
lines, and is not in `PLUGIN_DIRS` — the CMake comment is explicit that the
omissions there are load-bearing. Options are to revive it, to vendor a small
radix-2 implementation (~200 lines, no dependency, plenty for 1024 or 2048
bins), or to add a dependency on kissfft or FFTW. **Vendor a radix-2.** A
dependency is not worth it for a display, and `-ffast-math` is now off by
default (`CMakeLists.txt:52`) so the arithmetic is at least predictable.
Whether `plugins/fft/dsp.c` is that radix-2 already is a twenty-minute
question and should be asked before writing a new one.

## 5. On the canvas

Inline where the node is, enlargeable.

The attached-control machinery from `NODE_EDITOR.md` §12 is the precedent and
mostly the mechanism: a `Box` with `attachedTo` set stacks above its host, is
laid out with it, and is drawn by `NodeCanvas::drawAttached()` with no title
bar. A probe panel is another kind of attachment — same stacking, same
"belongs to that node" reading, a different body.

Width is the constraint. §14 and §15 of that document are a sustained argument
that width is the axis this layout runs out of: boxes are 128px, twenty-five of
eighty-one graphs already exceed 1600px, and wrapping was measured and rejected.
So a probe panel is **128 wide, matching its host**, with height per visual
(meter 20, scope 64, spectrogram 96). That is small for a spectrogram, which is
what the enlarged view is for: double-click detaches the probe into a resizable
window at whatever size is useful, still fed by the same instance.

### The redraw problem

`NodeCanvas` has no timer today; every redraw is provoked by an event. Live
visualizers need roughly 30fps, and GTK4's `queue_draw()` invalidates the whole
widget — so the naive version repaints a 2900×540 graph thirty times a second
to animate a 128×64 rectangle.

Two mitigations, in order:

1. **Cache each probe into its own `Cairo::ImageSurface`.** The tick calls
   `visual_draw` into the surface; `on_draw` paints surfaces. The graph itself
   is then the only thing being re-rasterised, and a static graph re-blitted at
   30fps may simply be fine at these sizes. This is measurable before it is
   built, with a throwaway that calls `queue_draw()` on a timer over
   `dsp/old/bd9.dsp` — the widest graph in the corpus — and counts frames.
2. **If it is not fine:** a small `Gtk::DrawingArea` per probe in an overlay,
   positioned over the canvas, so GTK invalidates only that region. It costs
   repositioning on every scroll, zoom and relayout, which is why it is the
   fallback and not the plan.

The tick runs only while at least one probe is armed and the window is mapped.
An editor with no probes should cost exactly what it costs today.

### Arming one

Right-click an output port, or right-click a wire (which probes its source
port — a wire *is* a source port, so there is nothing else it could mean), and
pick a visual from a submenu built from the same catalogue scan `NodePalette`
uses. The palette grows a "Visualizers" section alongside "Controls", which is
already the precedent for a palette entry that is not a DSP plugin.

## 6. Persistence

`# @probe <node> <arg> <visual> [<option>=<value> ...]`, written by
`NodeLayout` into the same block it already owns.

This is the established mechanism and it has already been through the fire:
§8 of `NODE_EDITOR.md` records that the layout block has to be idempotent
(anchored on one prefix, prose included, or every save adds two lines) and that
the round-trip check has to scramble positions first or a save that silently
does nothing still passes. Both lessons apply unchanged.

The consequence worth stating plainly: **a probe travels with the patch but is
invisible to everything except the editor.** The parser sees a comment, every
other tool sees a comment, and a `.dsp` with probes in it loads identically in
a build that has never heard of them. That is the same bargain `# @layout`
struck.

## 7. Verification

The pattern this tree uses — a headless harness per claim, confirmed to fail
before it is trusted to pass — applies to three of the four pieces. The fourth
needs you.

**`scripts/dspprobe`** — the tap is correct. For every shipped `.dsp`, arm a
probe on every declared output port (86 across 63 plugins), render a chord, and
compare the tap's output against a single-threaded reference that walks the
same notes and sums the same args directly. The properties: the sum matches
bitwise; a probe on an unreachable node yields nothing rather than garbage; a
scalar arg reads as its constant; an `ARG_STATE` arg cannot be armed at all;
and dropped windows are counted rather than silently skipped.

**`dspstress` level 5, `probes`** — the tap is race-free. The existing harness
already runs a synthetic audio thread against a GUI thread doing GUI things,
in a forked child with a watchdog, under ThreadSanitizer. Arming and disarming
probes while notes play is one more level in that table. This is the gate that
matters most: the tap is new code inside the audio callback, and
`REVIVAL.md` §7 is a long account of what it costs to get that wrong.

**`scripts/visualcheck`** — the plugins do not crash and are deterministic. Load
every `visual/*.so`, open an instance, feed it the pathological cases (silence,
DC, a single sample, `NaN`, `±inf`, values at 1e5 like the four diverging DSPs
in `REVIVAL.md` §6, a length-1 buffer, sample rate 0), draw to image surfaces at
several sizes including 1×1 and something absurd, and assert no crash and that
identical input gives identical pixels. Needs no display, so it gates on all
three CI runners.

**`dspgraph`** — the panels behave. Probe panels sit against their host and
overlap nothing, hit-test where they are drawn, and survive a
write/read/apply cycle without changing any other line — the same invariants
already asserted for attached controls, plus a no-op write of an existing
`# @probe` line being byte-identical.

**And you.** §7 of `NODE_EDITOR.md` is right that the bugs that matter in this
part of the tree are visual and interactive, and that both real bugs in the
gtkmm port came from use rather than from anything catchable headlessly. A
scope that is subtly out of sync, a spectrogram that scrolls at the wrong rate,
a trigger that will not lock — none of those fail a test.

## 8. Order

1. **`thSampleRing`**, with a standalone test. Small, and everything else sits
   on it.
2. **The tap** — probe table, `SET_PROBE`, the accumulate in both note loops —
   plus `dspprobe` and `dspstress` level 5. No GUI at all. This is the piece
   with real risk and it can be finished and proven before anything is drawn.
3. **The visual ABI and `visual/meter`**, plus `visualcheck`. Smallest possible
   consumer, so the ABI's mistakes surface against forty lines.
4. **The canvas** — panels, arming, the frame tick — with the repaint
   measurement in §5 taken *first*, since it can change the design.
5. **Persistence**, `# @probe` through `NodeLayout`, and the `dspgraph` checks.
6. **`visual/scope`.** The first one anybody actually wants, and the first
   point at which this is worth showing to you.
7. **`visual/spectrum`**, once the FFT question in §4 is settled.
8. **`visual/spectrogram`**, the most expensive and the most likely to need the
   §5 fallback.
9. **The enlarged view.**

Steps 1–3 are worth doing regardless of what happens to the rest: they are all
headlessly testable, and step 4's measurement is the one that decides whether
the canvas plan survives contact.

## 9. Risks

- **The 30fps full-canvas repaint.** Measured in step 4, before the design
  depends on the answer. Fallback in §5.
- **Cairo in the plugin layer.** Contained to `visual/`, but it is a new
  dependency edge in a tree that is mid-port to two more platforms
  (`PORTING.md`). Cairo ships with GTK on all three, so the risk is packaging
  and CMake spelling rather than availability — the same class of problem as
  the RtAudio spelling that broke CI, and worth doing the same way (one
  spelling, checked on all three runners).
- **Probe invalidation.** `(nodeId, argIndex)` is only meaningful against the
  loaded tree, and a probe that survives a reload pointing at the wrong node
  would be a display that confidently shows the wrong signal. Disarm on
  `SET_CHANNEL`, re-resolve by name. This is the failure mode most likely to
  be believed rather than noticed.
- **"Not attached to a channel."** The editor works on a scratch copy and is
  only live when the channel is playing that file (`NODE_EDITOR.md` §10). A
  probe in an unattached editor has nothing to show, and the status bar has to
  say which case it is in as clearly as it already does for controls.
- **What the sum means.** Summing voices is what "the signal at this point" has
  to mean, but it is not what a single voice looks like: ten voices through a
  filter is ten filters, and the scope shows their sum, not the filter's
  response. That is correct and it will still surprise. A per-probe "newest
  voice only" toggle is the obvious answer if it does, and the tap design
  admits one for free — it is a different accumulate, not a different
  architecture.
- **Probing changes timing, if not sound.** The tap is a read and cannot alter
  a buffer, but it does add work to the callback. Four probes across ten voices
  is not a plausible xrun, and `dspblock` exists to say so if it ever is.
