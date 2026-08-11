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

---

## 10. Progress

Steps 1–3 of §8 are done, on `node-visualizers` off `patch-selector`.

| Step | State | Where |
|---|---|---|
| 1. `thSampleRing` | done | `libthink/thSampleRing.h`, `scripts/ringcheck` |
| 2. The tap | done | `libthink/thProbe.{h,cpp}`, `thSynth::armProbe`, `scripts/dspprobe`, `dspstress` level 5 |
| 3. The visual ABI and `visual/meter` | done | `src/thVisual.{h,cpp}`, `plugins/visual/meter.cpp`, `scripts/visualcheck` |
| 4. The canvas | done | `NodeGraph` probe boxes, `NodeCanvas::drawProbe`, `NodeEditor` |
| 5. Persistence | done | `# @probe` through `NodeLayout` |

Three CTest gates added — `ringcheck`, `dspprobe`, `visualcheck` — bringing the
suite to eight. All eight pass, and the ASan and TSan trees are clean.

```
dspprobe    2650 output ports across 81 files, 2667 checks, 0 failed
ringcheck   45 checks, 0 failed
visualcheck 39 checks, 0 failed
dspstress   5/5 levels clean under ThreadSanitizer
```

### The claim in §2 held, and is not this work's to defend

Node ids and arg indices do survive the per-note tree copy, so a probe really
is two array subscripts per voice. What the harness found is that this is not a
property anyone needs to preserve *for the tap*: the audio path resolves every
pointer arg through the same ids and indices, so breaking either one segfaults
`dspcheck` long before a probe could draw the wrong picture. The tap is riding
on load-bearing structure. `dspprobe` says so rather than claiming credit for
catching it.

### Two things the corpus decided

**The io node's `note`, `velocity` and `trigger` did not exist until a note
did.** `thMidiNote` writes all three into its *copy* of the tree. In the 5 of
92 files that never reference them, the arg existed only inside each copy —
taking a fresh index out of `addArgToIndex`, agreeing between copies only
because every copy starts from the same `argCount_`. Nothing outside a copy
could name them, so a probe on `ionode.note` resolved against the prototype and
found nothing while the node editor drew the port regardless. `finishParse` now
declares them, which is what `buildArgMap` already does for a plugin arg a
`.dsp` omitted.

It has to happen *after* `setPointers()`. `buildArgMap` resets `argCount_` to
zero and then indexes only args whose index is still negative, so the counter
is not settled until `setPointers` has created args for the references it
resolves — and a `.dsp` with a typo in one of those creates one. `harpsi0.dsp`
reads `ionode->bandlo` where its io node declares `bandlow`; done any earlier,
that typo and `note` shared index 21 and a probe on `ionode.bandlo` read back
the note numbers. That was found by `dspprobe`, on its second run.

**A channel serial, not a generation counter.** The first design had the audio
thread bump a per-channel generation on `SET_CHANNEL` and the GUI read it when
arming. It never matched: `loadTree` queues the swap, so the GUI reads the
generation *before* the audio thread has applied the change, and every probe
armed on a freshly loaded patch was stale from birth and published silence
forever. The handle has to be something the GUI already holds, so it is a
serial on the `thMidiChan` itself — monotonic, never reused, and therefore also
immune to the replacement channel landing on the freed one's address.

### What the tap cost

Nothing measurable. Four probes across ten voices is 40 lookups and 40k float
adds per 1024-sample window; `dsplevel`, `dspblock` and `dsplive` are unchanged
across the corpus with probes compiled in.

### On the ABI

`thVisual` lives in `src/`, not `libthink/`. Putting it in the engine would put
cairo behind every headless harness that links it, which is the opposite of the
containment §4 promised. A visual module links neither `libthink` nor sigc++ —
`meter.so`'s only shared dependency is `libcairo`, and it exports exactly the
six entry points and the version byte.

`thVisual::draw` clips to the panel and `save`/`restore`s around the call. A
module that leaves the context dirty or draws outside its box is then a bug in
that module rather than a smear across the canvas.

`thDynLib`'s four calls are `THINK_API` now. They were `thPlugin`'s private
business and `libthink` builds hidden-by-default; `thVisual` needs the same
four, and a second dlopen shim is precisely what that header exists to avoid.

### What the checks do not cover, stated rather than implied

- `visualcheck` proves a module does not crash, does not depend on where the
  feed was split, and draws the same thing twice. It does **not** prove the
  numbers are right: a meter fed 1e5 is asserted to have drawn something, not
  to have said +105 dB. Deleting `meter`'s guard against a sample rate of zero
  passes — the decay constants collapse and the display is wrong but perfectly
  deterministic. Whether a visualizer is *correct* is a matter of looking at it.
- `dspprobe`'s reference goes blind the moment a note is retired, because
  `thMidiChan` does that inside `process()`. The comparison stops at that
  boundary rather than pretending to cover it. Releases are covered by
  construction — the accumulate is inside both note loops — and by a separate
  check that re-strikes a sounding note and counts the voices on
  `ionode.note`: 60+64+67+60 = 251 with the decaying loop, 191 without.
- The channel-serial belt is deliberately untested. Disarming on reload is what
  actually keeps a probe off a replaced tree and that *is* tested; the serial
  exists for a future caller who forgets, and there is no way to reach that
  state through the public API — which is the point of it.

### Portability, found by CI rather than by argument

Three things, all in `visualcheck`, all worth recording because each was a
guess that a second platform disproved:

- A hand-rolled directory walk matching `.so` builds cleanly on macOS and then
  reports "no visual modules": the suffix is `.dylib` there and `.dll` on
  Windows. `config.h`'s `PLUGIN_SUFFIX` already knew.
- `M_PI` is not in C++. glibc provides it from `<math.h>`; MinGW's UCRT runtime
  does not without `_USE_MATH_DEFINES`. `think.h` does that dance for the 27
  places in the engine that need it, and a harness deliberately free of
  `think.h` does not inherit it.
- `cairo_select_font_face` pulls in fontconfig, whose global pattern cache is
  never freed — 4738 bytes in 68 allocations, and an ASan job that runs with
  `detect_leaks=1` goes red. Suppressed by module through
  `__lsan_default_suppressions`, the same way `dspstress` supplies
  `__tsan_default_options`, so a real leak still fails: a deliberate 777-byte
  leak in `visual_open` is reported.

## 11. The repaint measurement, and what it settles

§5 said the 30fps full-canvas repaint had to be measured before the canvas was
designed, because the answer decides between caching each probe into an image
surface and giving each one its own `DrawingArea` in an overlay. Measured, with
`scripts/canvasbench`:

```
                              graph        mean     worst
dsp/old/bd9.dsp    45 boxes  2920x576    0.80 ms   1.75 ms
dsp/aspect2.dsp    53 boxes  1716x1048   0.63 ms   1.68 ms
dsp/ts1.dsp        28 boxes  1888x490    0.36 ms   1.46 ms

15 graphs, viewport 1200x700:  mean 0.39 ms, worst 0.61 ms
```

**A full repaint costs about 2% of a 33ms frame, and the worst graph in the
corpus costs under 3%.** So the canvas can simply redraw, and the overlay
fallback — with its repositioning on every scroll, zoom and relayout — is not
needed and is dropped from the plan.

The second column is zoomed to fit, so the whole graph rasterises rather than
just the part inside the viewport; that is why `bd9` at 2920 pixels wide costs
what it costs. The unzoomed numbers are lower because GTK clips to the window,
which is also what actually happens in use.

This changes the standing of the per-probe surface cache: it is now an
optimisation rather than the thing that makes the design work. Worth keeping
for the spectrogram, which does real work per frame, and not worth reaching for
before that.

`canvasbench` drives the real `NodeCanvas` in a real window rather than a
stand-in drawing something of similar complexity — the same reasoning as §12 of
`NODE_EDITOR.md`, where describing one cubic twice is how "clicking a wire
selects a different wire" happened. That makes it the only harness in the tree
that needs a display, so it is a measuring instrument rather than a gate and is
not in the ctest list. Under a headless box it runs under `xvfb-run`.

Two things it took a couple of tries to get right, both recorded because they
made the first numbers meaningless rather than wrong-looking:

- **A widget does not draw until it is realized and mapped.** Creating the
  application and pumping the default main context measured every graph as zero
  frames, correctly. It has to go through `activate()`.
- **Non-blocking iteration never lets a frame happen.** Between `queue_draw()`
  and the frame clock deciding to render, nothing is pending, so a
  non-blocking spin returns immediately every time and the loop expires. The
  pump blocks, with a deadline.

## 12. The canvas, and what a probe turns out to be

Three things at once, and `NodeEditor` is the only place they meet: a `Box` in
the graph, a slot in the engine, and an instance of a visual module. It holds
them keyed on **node and arg name**, because those are the only part that
survives what the user does — a reload renumbers every box and drops every
tap, so both are rebuilt from the names on every reparse.

Right-click an output port to pick a module; right-click a panel to stop
watching. Right-click because it is the one gesture the canvas does not
already spend: left is wire, drag and slider, and the wheel is scroll and zoom.

The panel is a `Box` rather than a structure of its own, so it inherits the
attachment stacking, the overlap invariants and the hit-testing `dspgraph`
already asserts over every box. Two things in the stack had to generalise: its
height was rows × `ATTACH_H`, which is fine while every row is a slider and
wrong the moment a 24-pixel meter and a 96-pixel spectrogram join it; and
`assignAttachments` cleared every host before working them out, which is right
for a control — its host is whichever node turns out to be its only consumer —
and wrong for a probe, which knows its host from the moment it is armed.

### A panel is not joined to its host by a wire

§12 of `NODE_EDITOR.md` argued that a control needs its wire drawn because
adjacency cannot say which of several inputs it drives. The same argument does
not carry: an `Edge` would appear in `edges()`, which every consumer reads as
"a connection in the `.dsp`" and which `NodeEdit` would try to write. A probe
is not in the file's graph at all. The panel says which output it reads in its
own title row instead — which a slider strip could not do, because its text is
already the label and the value.

### Persistence

```
# @probe adsr out meter
# @probe adsr play meter
# @probe mixer out meter
```

In the block `# @layout` already owns, stripped and rewritten with it, so a
save stays idempotent — that is §8's lesson and it applies unchanged. No
position: a panel is wherever its host ended up, computed every time, for the
same reason attached controls are not written either.

A probe names a module the running build may not have. That is reported rather
than dropped: a panel simply not appearing reads as the file having lost it.

## 13. What the checks cover now

Nine CTest gates. The three that are new to this work:

```
dspprobe    2650 output ports across 81 files, 2667 checks
dspgraph    2650 panels armed and removed; 374 probes written and read back,
            and every file still writes none when it has none
editorcheck 25 checks, end to end
visualcheck 39 checks, ten pathological feeds at six sizes
ringcheck   45 checks
```

`editorcheck` is the one worth explaining. The others each cover one link —
`dspprobe` the tap, `visualcheck` the module, `dspgraph` the panel — and none
of them says the three are joined up. So it opens a patch on a live channel,
arms through the same call the menu makes, plays a chord, and asserts the
module draws **differently from one that has been fed nothing**. Against a
fresh instance rather than against a blank image, because the frame, the knee
tick and the text are drawn whatever the signal is, and comparing to blank
would pass on silence. Then it saves, reopens, and checks the probe came back.

It needs a display, so it skips itself loudly where there is none and CI runs
the suite under `xvfb-run`. It is worth most on the ASan job, where it is the
only thing that exercises the teardown order — stop the tick, close the
instances, then unload the modules. A deliberately unclosed instance is a
48-byte leak and fails the run; fontconfig's and Mesa's 1.8MB of global caches
are suppressed by module so that it can.

Every new check was confirmed to fail first:

| break | caught by |
|---|---|
| the stack walked by multiplying `ATTACH_H` | `dspgraph`, 15 files on overlap |
| `assignAttachments` clearing probe hosts | `dspgraph`, every panel |
| the tick never feeding the module | `editorcheck` |
| a reload not putting the panels back | `editorcheck` |
| the file's probes never read on open | `editorcheck` |
| `# @probe` lines not stripped before rewriting | `dspgraph`, doubling per save |
| probes never written | `dspgraph` |
| an instance left open | `editorcheck` under ASan |

### One thing worth knowing about running an uninstalled build

`thPluginManager::resolveRoot` tries `./plugins` before the directory beside
the binary. A tree that once had an autotools build still has 62 stale `.so`
files in the *source* `plugins/`, which is enough to pass for a plugin root —
so running from the top of the tree loads every DSP plugin from there, and
`plugins/visual/` has only a `.cpp` in it. The symptom is "no visual modules",
the cause is that every plugin was already coming from the wrong place, and
the fix is `THINK_PLUGIN_PATH` or deleting the leftovers. The editor now names
the directory it searched, which turns that from a mystery into a sentence.

## 14. The four modules

All four are built. Each is one file in `plugins/visual/` that nothing else in
the tree knows about, which was the point of making them plugins.

| | what it answers | state |
|---|---|---|
| `meter` | how loud, and is it clipping | peak and RMS, dBFS, decaying peak |
| `scope` | what shape | triggered on a rising zero crossing |
| `spectrum` | what is in it | 1024-point FFT, log axis, dBFS |
| `spectrogram` | what it does over time | 512-point, 4× overlap, 256 columns |

### The scope's whole difficulty is one problem

A scope that draws the newest N samples every frame shows a waveform sliding
sideways at the beat between the signal and the refresh rate. It is unreadable,
and it looks like a bug in the synth rather than in the display.

The trigger is a rising zero crossing, searched **at draw time rather than
remembered at feed time** — which is what keeps the picture independent of
where the drain's boundaries fell. Searched oldest-to-newest keeping the newest
hit, rather than backwards stopping at the first: backwards locks onto the last
crossing before the window, which on anything but a pure tone jitters between
harmonics. Zero rather than a level, because a level that suits an envelope's
output suits nothing else, and a signal that never reaches it never triggers.

Drawn as the min and max of each pixel column rather than a polyline through
every other sample, which is what makes a square wave look square.

### The FFT question, answered

§4 left reusing `plugins/fft/dsp.c` as a twenty-minute question. Asked: it does
hold a working radix-2 — Embree and Kimble's — and the answer is still no, for
reasons about its shape rather than its arithmetic. Its twiddle table is a
function-static keyed on the last size it was asked for, so two instances at
different sizes rebuild each other's table on every call; and it calls
`exit(1)` when a `calloc` fails, which is a library killing the host process —
the exact pattern `REVIVAL.md` records removing from the parser. Every probe
here is its own instance and there may be eight.

So `plugins/visual/fftr.h`: eighty lines, one instance's worth of state, no
globals, nothing that can fail after construction, and a scale factor that
undoes Hann's coherent gain so a full-scale sine reads 1.0 and the dB axis
means what it says. No dependency, because the reason to take one would be
speed and 15k butterflies per frame is not a speed problem.

**This is the one place in the whole of this work where "are the numbers right"
has an answer, so it gets asked.** `visualcheck` now checks the transform
numerically: a sine on bin *k* peaks at bin *k*, reads 1.0, and leaves nothing
above −40 dB outside its main lobe; DC lands in bin 0; silence transforms to
silence. Halving the scale factor fails 4 checks; an off-by-one in the bit
reversal fails 13. A spectrum with every peak one bin low, or 6 dB down, is not
something anyone would catch by eye.

### The spectrogram transforms on sample count, not on frames

The decision the module turns on. A spectrogram that transformed in `draw()`
would produce columns at the *frame* rate — so a stall would compress a second
of audio into one column, and the picture would be a record of the GUI's
scheduling rather than of the sound. Hopping on sample count means the time
axis belongs to the signal whatever the display does, and it is also what makes
the module indifferent to where the drain split the audio.

Drawn by building one image surface and painting it scaled, rather than 32768
rectangle fills — that being the one thing in these four modules that would
actually have shown up against the budget §11 measured.

### What it costs

Seventeen spectrogram panels on `ts1.dsp`, which is more than the engine's
eight-probe limit allows:

```
meter        0.56 ms   (1.7% of a 33ms frame)
spectrogram  2.40 ms   (7.3%)
```

So the most expensive module, at twice the number of panels anyone can have,
is under a tenth of a 30fps budget. §11's conclusion holds and the surface
cache is still not needed.

### One thing the battery was missing

Every feed in `visualcheck` was stationary, so a module that ignored the time
axis entirely would have drawn all of them correctly — and the spectrogram is
the one module whose whole purpose is that axis. There is a rising exponential
sweep now, 16384 samples of 200 Hz to 8 kHz, which is both what anyone would
actually point a spectrogram at and a much harder case for the split-feed
comparison. It draws as a straight diagonal, which is what an exponential sweep
on a log axis should be.

## 15. The enlarged view

A panel is 128 pixels wide because a node box is, and a spectrogram at that
size is a thumbnail. Double-clicking one opens a resizable window at 512×320,
closed with Ctrl+W like every other secondary window here.

**The same instance, drawn twice — not a second instance on the same point.**
Two would each need feeding and would then diverge: different histories,
different trigger points, a scope in the window showing a different cycle from
the scope on the canvas. One instance is both cheaper and the only version that
can be trusted to agree with the panel it came from.

Everything about the window is keyed on the probe's node and arg name, like
everything else about a probe. The first version bound each window's draw
function to an index into the list, which meant closing one window renumbered
the rest and every close had to rebind them — a loop that existed only to keep
a number correct. Names need no such loop.

The frame tick is what closes a window whose probe has gone, rather than
`disarmProbe` doing it: a probe removed by a *reload* never goes through
`disarmProbe`, and a window left drawing through a closed instance is a
use-after-free rather than a cosmetic problem. The tick therefore keeps running
while any window is open even with no probes left, so that something notices.

`editorcheck` covers it, and the three ways it can go wrong were each broken on
purpose first: opening the same panel twice opening two windows, a dead window
never being closed, and the by-name lookup matching the wrong probe. All three
fail exactly one check.

### Still ahead

Nothing outstanding. The next thing is whichever module turns out to be missing
once these have been used in anger — which is a question that wants a session
with the editor rather than another one with the corpus.
