# Visualizers

Live displays of the signal at any point inside a patch: a level meter, an
oscilloscope, a spectrum, a spectrogram. Right-click an output port in the node
editor, pick one, and watch it while you play.

This document is two things. The first half is what you need in order to write
one — the ABI, the rules, the harness. The second half is why it is shaped the
way it is, because several of those decisions look arbitrary until you know what
they cost.

The work was scoped, argued and measured across eight branches; this replaces
the running log that lived here while that happened. `git log
patch-selector..visualizers-enlarged` has the detail, and the commit messages
are where the reasoning per change lives.

---

## 1. What a visualizer is

A **probe** is a tap on one node's output: `filt.out`, `env.play`,
`ionode.note`. It is armed from the editor, summed across every sounding voice,
and published to the GUI a window at a time.

A **visual module** is a plugin that turns those samples into pixels. It is a
shared object in `plugins/visual/`, loaded by the GUI, and it is the only part
of this you need to touch to add a new kind of display.

The two are deliberately separate. The tap lives in `libthink` because it runs
inside the audio callback; the drawing lives in a plugin because drawing is
where the variety is. Four modules ship:

| module | shows | asks for |
|---|---|---|
| `meter` | peak and RMS, dBFS | 128×24 |
| `scope` | the waveform, triggered | 128×56 |
| `spectrum` | frequency content, log axis | 128×64 |
| `spectrogram` | the spectrum over time | 128×80 |

A probe is **not** a node. It does not appear in the `.dsp`'s graph, it cannot
change the sound, and arming one costs no rewiring and no reload. It is
recorded in the file as a comment:

```
# @probe filt out spectrum
```

so it travels with the patch and is invisible to everything that is not the
editor — the same bargain `# @layout` struck for node positions.

---

## 2. Writing a visual module

One file in `plugins/visual/`, one line in `plugins/CMakeLists.txt`. Start by
reading `plugins/visual/meter.cpp`: it is the smallest complete example, and it
is commented for exactly this purpose.

### The ABI

Declared in `src/thVisual.h`. Six entry points, `extern "C"`:

```c
int   visual_init    (thVisual *v);
void *visual_open    (thVisual *v, unsigned int samplerate);
int   visual_feed    (void *inst, const float *samples, unsigned int n);
int   visual_draw    (void *inst, cairo_t *cr, int w, int h);
void  visual_close   (void *inst);
void  visual_cleanup (thVisual *v);      /* optional */
```

`visual_init` runs once per module. It **must** call `setName()` and
`setDesc()` — the loader refuses a module that sets neither, because a nameless
row in a menu is worse than an absent one. It may call `setPreferredSize()`.

`visual_open` returns your instance, one per probe. `visual_close` frees it.

`visual_feed` gets samples; `visual_draw` gets a `cairo_t` clipped to a `w × h`
box at the origin. Both return 0 on success — the host reports the first
non-zero from each to stderr and then leaves you alone, so a module that refuses
every frame does not flood the log.

### Building it

```cmake
think_add_visual(myvisual)
```

That is the whole build integration. A visual module links **cairo and nothing
else** — not `libthink`, not sigc++. It has no node, no arg and no tree, which
is what stops a visualizer from reaching into the graph it is drawing.

### Five rules

**1. You are on the GUI thread.** Allocate freely, keep history, use the C++
library, take as long as a frame allows. None of the real-time discipline that
governs `plugins/osc` applies here. Worth saying, because the opposite habit is
well established everywhere else in this tree.

**2. Do not care where the feed boundaries fell.** The tap publishes one window
at a time; the GUI drains however many are waiting. The same audio arrives split
differently on every frame. A module that treats a feed boundary as meaningful
looks fine in isolation and jitters in use — and `visualcheck` will fail it, by
feeding one instance a lump and another the same samples in pieces of 37 and
comparing the pixels.

**3. Drawing twice must draw the same thing.** `draw()` is called on a timer and
may be called twice with no feed in between. If it mutates state — decaying a
peak, advancing a scroll — the display flickers. Put that in `feed()`.
`visualcheck` fails this too.

**4. Handle what the corpus actually produces.** Silence, DC, a single sample,
denormals, ±inf, NaN, and values around 1e5 — four shipped DSPs have diverging
filters, and `mixer.out` on `dsp/noargs/bd1.dsp` reaches −inf within seven
windows. The NaN case is the one that bites: every comparison against a NaN is
false, so peak tracking written the obvious way silently ignores it and the
display reads a confident zero for a signal that has blown up. All four shipped
modules say `NOT FINITE` rather than filtering.

**5. Allocate with `std::nothrow`.** `visual_open` is reached through a function
pointer. A `bad_alloc` thrown there unwinds across a C ABI boundary, out of a
`dlopen`'d module and into a host with no catch anywhere near it, and terminates
the process. A visualizer failing to allocate should cost a panel, not the
synth.

### Testing it

```sh
./build/scripts/visualcheck -p build/plugins/
```

Every module in the plugin directory, ten pathological feeds, six panel sizes
including 1×1, no display needed. It checks survival, the two determinism rules
above, and the ABI's return values.

It does **not** check that your numbers are right — it asserts that a meter fed
1e5 drew *something*, not that it said +105 dB. For that:

```sh
./build/scripts/visualcheck -p build/plugins/ -o /tmp/pics
```

which writes every module's drawing of every feed at 480×200, so you can look.
That is how the shipped modules were checked: the sine peaks where the log axis
says 689 Hz should be, the square shows odd harmonics only falling at 1/n, and
the sweep draws as a straight diagonal.

To see one at real size in a real canvas:

```sh
xvfb-run -a ./build/scripts/canvasbench -P -V myvisual -o /tmp/canvas.png \
    -p build/plugins/ dsp/ts1.dsp
```

### A shared FFT, if you need one

`plugins/visual/fftr.h` is a radix-2 with no global state, scaled so a
full-scale sine reads 1.0 in its bin. `spectrum` and `spectrogram` both use it,
and `visualcheck` checks it numerically — the one place in this whole feature
where "are the numbers right" has an answer.

Ignore `plugins/fft/dsp.c`. It does hold a working radix-2, and it keeps its
twiddle table in a function-static keyed on the last size it was asked for — so
two instances at different sizes rebuild each other's table on every call — and
it calls `exit(1)` when a `calloc` fails.

---

## 3. How it works underneath

You do not need this to write a module. You do need it to change the engine.

### The tap

A probe is `(channel, nodeId, argIndex)` plus a ring buffer. Resolving it in one
voice is two array subscripts, because both halves of the address survive the
per-note tree copy: `thNode`'s copy constructor carries the id, and the arg
index is carried twice over, by `thArg`'s copy constructor and by
`thNode::copyArgs`.

That is what makes summing across voices affordable inside the callback. Worst
case in the corpus — ten voices, four probes, a 1024-sample window — is 40
lookups and 40k float adds per window, against 44100 samples a second.

It is accumulated inside **both** note loops in `thMidiChan::process`, not after
that function returns: a note whose envelope ended this window is retired before
the call is over, so tapping afterwards would clip the last window off every
release.

Arming allocates on the GUI thread and hands the finished object over through
the command queue, the same way a note or a channel does. Eight slots, fixed.

### The handoff

`thSampleRing` — an SPSC ring like `thRing`, but `memcpy` in and out, so a
window costs one release store rather than 1024.

`write()` is **all-or-nothing**. A ring that took what fit would leave half a
window in the stream, and half a window is a splice that a spectrum will happily
transform and draw as though it were signal. What does not fit is dropped whole
and counted; the audio thread can neither block nor touch the consumer's tail.

### Invalidation

A probe's ids are only meaningful against the tree its channel is playing *now*.
Loading a patch invalidates every probe on that channel, so the editor disarms
before it queues the swap, and `thMidiChan` carries a serial no other channel
has ever had — a probe whose serial no longer matches contributes nothing. It
fails towards silence rather than towards a display confidently drawing the
wrong node. The editor notices the lost tap on its next frame and asks for
another.

Re-arming is by **name**, which is the only part of a probe that survives a
reparse. Everything about a probe is keyed on `(node, arg)` for that reason: the
panel, the instance, the enlarged window, the `# @probe` line.

### On the canvas

A panel is a `Box` in `NodeGraph` with `isProbe` set, attached to the node whose
output it reads and stacked above it alongside the control strips. It inherits
the layout, the overlap invariants and the hit-testing that `dspgraph` already
asserts over every box.

It is deliberately **not** joined to its host by an `Edge`. An `Edge` appears in
`edges()`, which every consumer reads as "a connection in the `.dsp`" and which
`NodeEdit` would try to write. The panel names the port it reads in its title
row instead.

Double-click a panel for a resizable window showing the same instance larger.
The same instance, emphatically — two would each need feeding and would then
diverge, so the scope in the window would show a different cycle from the scope
on the canvas.

---

## 4. Why some of this looks odd

**Why is a probe not a node in the `.dsp`?** Because inserting one would cost a
rewire and a reload. Wire changes reach the file only on Save and need the graph
rebuilt, so "put a scope on the filter output" would mean adding a node, cutting
a wire, laying two more, saving, and reloading the channel — losing every
ringing note. For something you reach for mid-tweak and drop a moment later that
is the wrong price. A node in the signal path could also perturb what it
measures.

**Why does the canvas just redraw?** Because it was measured before it was
designed. GTK4's `queue_draw()` invalidates the whole widget, so animating a
128×64 panel repaints the entire graph. The widest graph in the corpus —
`dsp/old/bd9.dsp`, 2920 px — repaints in **0.80 ms**, under 3% of a 30fps
budget, zoomed to fit so the whole thing rasterises. Seventeen spectrogram
panels on `ts1.dsp` cost 2.40 ms, at twice the number of probes the engine
allows. `scripts/canvasbench` is that measurement, and it can be re-run.

**Why cairo, rather than a pixel buffer or a fixed data model?** Both avoid the
dependency, and both mean a module cannot produce a look the canvas did not
anticipate — which is most of the point of making these plugins. A spectrogram
wants a pixel buffer, a scope wants crisp lines, a meter wants text. It is
contained: `thVisual` lives in `src/` rather than `libthink/`, so no headless
harness that links the engine picks up cairo.

**Why does the scope trigger?** A scope that draws the newest N samples every
frame shows a waveform sliding sideways at the beat between the signal and the
refresh rate. It is unreadable, and it looks like a bug in the synth rather than
in the display.

**Why does the spectrogram transform in `feed` and not in `draw`?** Because
transforming on demand would produce columns at the *frame* rate, so a GUI stall
would compress a second of audio into one column and the picture would be a
record of the scheduler rather than of the sound.

---

## 5. The checks

Nine CTest gates; five of them are this feature's.

| harness | covers |
|---|---|
| `ringcheck` | `thSampleRing`: wrap, overrun, ordering, threaded |
| `dspprobe` | the tap, against a reference built by name in each voice's own tree |
| `dspstress` level 5 | arming and draining while the patch underneath is replaced, under TSan |
| `visualcheck` | every module, ten feeds, six sizes; and the FFT numerically |
| `editorcheck` | the three ends joined up: tap, module and panel, end to end |
| `dspgraph` | panels laid out, hit-tested, and round-tripped through `# @probe` |

`canvasbench` is a measuring instrument rather than a gate, and is not in the
list. It and `editorcheck` need a display; `editorcheck` skips itself loudly
without one, and CI runs the suite under `xvfb-run`.

Every check here was **confirmed to fail before it was trusted to pass** — that
is the house style, and the deliberate breaks are recorded in each harness's
header comment. Two are worth repeating, because they say what the harnesses are
for:

- Clearing the RMS at the top of `feed()` fails 5 of `visualcheck`'s checks.
  That is the feed-boundary rule, and it is invisible by eye.
- Deleting the accumulate from `thMidiChan`'s decaying-note loop fails exactly
  one of `dspprobe`'s — the one that re-strikes a sounding note and counts
  voices on `ionode.note`: 60+64+67+60 = 251 with it, 191 without.

And two things the checks deliberately do **not** cover, stated rather than
implied. `visualcheck` cannot tell whether a module's numbers are right, only
that it is deterministic and does not crash. And whether a display is *legible*
is a matter of looking at it — which is what `-o` is for.

---

## 6. Adding a module: the short version

1. Copy `plugins/visual/meter.cpp` to `plugins/visual/yours.cpp`.
2. Add `think_add_visual(yours)` to `plugins/CMakeLists.txt`.
3. Build, then run `visualcheck -p build/plugins/` until it is green.
4. Run `visualcheck -p build/plugins/ -o /tmp/pics` and look at the pictures.
5. Try it in the editor: right-click an output port on a patch that is playing.

Nothing else in the tree needs to know it exists.

---

## 7. If the menu is empty

`thPluginManager::resolveRoot` tries `./plugins` before the directory beside the
binary. A tree that once had an autotools build still has stale `.so` files in
the *source* `plugins/`, which is enough to pass for a plugin root — so running
from the top of the tree loads every DSP plugin from there, and
`plugins/visual/` has only `.cpp` files in it. Either

```sh
THINK_PLUGIN_PATH=build/plugins ./build/src/thinksynth
```

or delete the leftovers, which nothing writes any more:

```sh
find plugins -name '*.so' -delete
```

The editor names the directory it searched, so the menu says which one it is.
