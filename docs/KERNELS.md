# Plugin kernels

A kernel is a plugin's work on one sample, apart from the plugin: a struct
holding the state the plugin keeps between samples, and a `tick()` from
plain floats to a float. The plugin runs the kernel over its window; a
patch compiled to a program inlines it. What plays live and what a
compiled patch plays are then the same arithmetic.

`osc::fmop`, `env::ad`, `env::adsr` and `misc::midi2freq` have kernels,
and the pattern is theirs:

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
- **A kernel that keeps nothing is a function**, `thk<Name>(...)`, as
  `thkMidi2freq` is. A plugin with two modes has a kernel for each, as
  `env::adsr` has `thkAdsrFree` and `thkAdsrGated`.
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
rate it is given, as the engine folds them at load. A patch using a
plugin with no kernel, or a loop, is refused with the node named, and so
is an effect, whose input a voice isn't given. `start()` makes the voice
new again, so a firmware can keep a pool of them; a knob's member is its
name after `k_` (`k_bright`), since a name may be one of C++'s.

Each knob comes with its range and a `set()` by name, for a controller
profile to ride. With `-c DEVICE` the patch's `controller DEVICE` block,
checked against that controller's controls, comes too, as `profile()`,
a `thcHidBlock` a `thhMapper` plays: a microcontroller needn't parse
one.

`scripts/compilecheck` holds two compiled voices -- `dsp/dxbell.dsp`, and
`scripts/compiled/gate.dsp` for a gated envelope's release -- to what the
engine renders of the same note, bit for bit. The knobs hold still there:
a knob turned while a note sounds is glided by the engine, and the
compiled voice doesn't glide yet. It also plays `dxbell`'s compiled
block for the BOX through a mapper.
