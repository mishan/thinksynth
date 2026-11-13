# Plugin kernels

A kernel is a plugin's work on one sample, apart from the plugin: a struct
holding the state the plugin keeps between samples, and a `tick()` from
plain floats to a float. The plugin runs the kernel over its window; a
patch compiled to a program inlines it. What plays live and what a
compiled patch plays are then the same arithmetic.

`osc::fmop`, `osc::simple`, `osc::blep`, `osc::noise`, `env::ad`,
`env::adsr`, `filt::svf`, `filt::eq`, `dist::saturate`, `mixer::pan`,
`math::sin`, `misc::midi2freq`, `misc::latch`, `delay::allpass` and
`delay::echo` have kernels, and the pattern is theirs:

- **`plugins/<category>/<name>.h`** holds the kernel, `struct thk<Name>`.
  It includes `libthink/thKernel.h` and nothing else of the engine's: no
  `thArg`, no window, no `think.h`, so a program with no engine can
  include it.
- **What a kernel makes of its slow inputs is a `Ctl`**: `thkFmop::ctl`
  turns a frequency and ratio into a phase step, `thkAd::ctl` and
  `thkAdsr` turn times into a sample's share of each segment. `tick()`
  takes the `Ctl`. The plugin makes one a window while those inputs
  hold one value, and one a sample while they move; a compiled voice
  makes one a call while they're knobs, numbers or the note's. The same
  arithmetic either way, so the two still render alike to the bit.
- **What runs once in a `Ctl` may afford libm**, where the accuracy is
  worth more than the cycles: `thkPan::ctl` uses `sinf` and `cosf`, since
  the table's rounding on each side is more than constant power allows.
  A compiled voice hands such a call its numbers through `thkHeld`, or
  the C++ compiler would work it out at build time, correctly rounded,
  where the library's answer at run time can be an ulp away.
- **A kernel that keeps nothing is a function**, `thk<Name>(...)`, as
  `thkMidi2freq` is. A plugin with two modes has a kernel for each, as
  `env::adsr` has `thkAdsrFree` and `thkAdsrGated`.
- **A delay's line is the caller's**: `thkAllpass` and `thkEcho` keep
  only their position and filters, and take the line and its length,
  zeroed by the caller when the length changes. A firmware's memory is
  its own to find, and the plugin keeps resizing its state arg.
- **Float throughout.** The FPUs of the small chips a compiled patch is
  for are single precision; a `double` there is a call into software,
  ten to twenty times slower. A plugin that kept a sum in float on
  purpose (a phase, so a window boundary is no event) keeps doing so.
- **The plugin's `.cpp`** keeps its description, its args and the long
  comment saying why the plugin is built as it is; its callback reads
  the state into the kernel, runs `tick()` over the window and stores the
  state back.
- **`libthink/thKernel.h`** holds what kernels share: `thkFinite`,
  `thkClamp` and `thkBoundFreq`, the float spellings of the engine's
  guards, and `thkSin2pi`, a sine taking its angle in turns from a
  table of 2048 to the turn and a line between neighbors: within 1.2e-6
  of `sin()`, exact at 0, its crest and its trough, so an envelope built
  on half a sine peaks at its peak. A table read and one multiply-add,
  where a series is a dozen in a row, each waiting on the last on a
  small FPU (61 cycles a sine on the ESP32-S3, 111 as a series).
- **`scripts/kernelcheck`** holds each kernel against a double reference
  of the same formula, and the change to what the corpus renders is
  measured with `scripts/dspab -a OLD_PLUGINS -b NEW_PLUGINS` and said in
  the commit. dspab's figure is the worst single sample, and a resonant
  filter or a feedback loop can magnify a float's rounding into a large
  one; where it is large, compare the difference's energy with the
  signal's over a longer render (`scripts/dspbench -o`) before calling it
  a change in the sound.

A kernel included by more than one plugin's source goes in the static
build's prelude, `plugins/thinkstatic.h`, as `thKernel.h` does: the
static build wraps each plugin in a namespace of its own, and a header's
guard would otherwise keep it out of every namespace but the first.

## Compiled voices

`scripts/dspcompile [-p plugins/] [-r rate] file.dsp Name` writes a patch
as `struct thvName`: one voice, the state of each of its nodes' kernels,
and a `render()` that runs them in the graph's order a sample at a time,
with what depends only on numbers, knobs and the note worked out once a
call, as the engine works it out once a window. Times are samples at the
rate it is given, as the engine folds them at load, and `bind(rate)`
folds a node's times again for a voice to run at another rate, as the
engine at that rate would; a knob's default and range stay at the
compiled rate. A patch using a
plugin with no kernel, or a loop, is refused with the node named. An
effect compiles too: its `render()` takes `in[channel]`, the io's `in<N>`,
and one that reads anything else the engine hands an effect is refused.
A delay's line is the voice's, sized and zeroed as the plugin's state is
when its length changes. `start()` makes the voice
new again, so a firmware can keep a pool of them, and seeds its noises:
`start(note, velocity, seed)`, the seed 0 if none is given, so a
firmware passes each voice a seed of its own or every voice's noise is
the same stream. A knob's member is its name after `k_` (`k_bright`),
since a name may be one of C++'s.

A patch that reads `misc::scan` is a video patch, on a raster: its voice
has `raster(w, h)`, which it must be given before it renders, and its
`render()` takes `at`, the raster's sample the call begins on, as scan
reads the synth's position. Each value is worked out as seldom as what it
reads changes: once a call; in a table a line wide, once a call for what
reads only the column, once a frame for what reads the column and the
frame (scan's `t`); once a line for what reads the row or the frame; and
every pixel for the rest. Each value's statements are as they'd be every
sample, never rearranged, so it is the same bits wherever it's worked
out, and compilecheck holds every patch in `video/` to the engine's
pixels at 160x120 and 80x60.

Each knob comes with its range and a `set()` by name, for a controller
profile to ride. With `-c DEVICE,...`, for each device named, the
patch's `controller DEVICE` block over the device's own, checked against
that controller's controls, comes too, as `profile_DEVICE()`, a
`thcHidBlock` a `thhMapper` plays: a microcontroller needn't parse one.

`scripts/compilecheck` holds compiled voices -- `dsp/dxbell.dsp`,
`dsp/grab.dsp`, `dsp/index.dsp`, `scripts/compiled/gate.dsp` for a gated
envelope's release and `scripts/compiled/names.dsp` for knobs named for
C++'s words -- to what the engine renders of the same note, bit for bit,
at their knobs' defaults and with the knobs turned. The knobs hold still
there: where a knob turned while a note sounds reaches `mixer::pan`'s
`pan`, `mixer::mul`'s inputs, `filt::svf`'s cutoff and resonance or
`filt::eq`'s freq, gain and q, the engine glides it over 5 ms, and a compiled voice steps to it at its next
call. Nor is a voice's noise the engine's stream, which comes from the
synth's: `index` is held to the bit with its Air, all of its noise, at
0, and `scripts/compiled/noise.dsp`'s compiled noise to the engine's by
each color's level and slope. It also plays `dxbell`'s compiled block
for the BOX through a mapper.

### In the engine

With `--engine`, a voice comes with `thv<Name>Engine`, which a tree runs
in place of its nodes (`libthink/thCompiled.h`). The build compiles the
patches in `video/` and `video/fx/`, and `dxbell`, `glass` and `rhodes`
from `dsp/`, into `think_compiled`, whose `thCompiledInstall()` a
program calls to have them; `genwav` does, and `--interpret` keeps it
from, and its summary says which way the picture ran. The browser
module's worklet and video worker do too, built from the native tree's
`think_compiled` sources (`THINK_NATIVE_BUILD`, `build/` by default);
without them their graphs run on their nodes.

An instrument compiled gains far less than a picture: a window of audio
repeats nothing across a raster for the compiler to work out once, and
the nodes already run a window at a time. `rhodes` alone renders 7%
faster in `genwav`, and on the page a chord held over it takes a third
less a quantum; it adds 14 KB to the module. The other instruments wait
on kernels (`mixer::fade`, `env::map`, ...) and on `osc::noise` and a
knob's glide, which `dspcompile` refuses.

- **Which graph.** A tree's fingerprint (`thSynthTree::fingerprint`) is
  its graph as parsed, as text: each node, its plugin, and its args, with
  times as written and without the io args the engine writes as it
  plays. A compiled graph is added by its own, so a patch edited by one
  number, or loaded from another file that says something else, finds
  none and runs on its nodes.
- **When.** A channel or an effect takes one once its tree is built, if
  it binds: the synth has a raster, and an effect is as wide as it was
  compiled. A voice clones its channel's, and a recycled voice's is reset.
  A stop resets an effect's, as it clears the effect's state.
- **Each window** it reads the io's note, velocity, trigger and aux, and
  each knob, as the plugins read them, and writes the io's outputs and
  `play`, so everything above the tree -- mixing, a voice's end, a
  stolen voice's fade, the effect chain -- is as it was.
- **Refused:** an `osc::noise`, since the engine's noise is the synth's
  stream, and an arg the engine glides because a knob feeds it.
- **A value changed in place** (the node editor's live edit, a piece's
  `reshape`), or a probe armed on the channel, drops the tree's compiled
  graph, and voices made after it run on their nodes.

`scripts/compiledcheck` plays one script -- a chord, a release mid-frame,
the pedal, a bend and an aux, the music's knobs moving, more notes than
the voice pool, a stop -- through every patch in `video/`, rings under
each effect in `video/fx/`, and `scripts/compiled/expression.dsp` for what
none of those read, at 160x120 and 80x60, before anything is installed
and after. Every frame is the same bits, and no window after runs on the
nodes. It also checks the fingerprint and each way a graph falls back.
The same goes for random graphs from `scripts/dsprandom`, at 80x60:
every kernel above wired to the raster, the note and the knobs and to
each other, so that what a value depends on is mixed in ways no shipped
patch is. `THINK_RANDOM_GRAPHS` (48) says how many.
