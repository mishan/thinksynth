# Plugin kernels

A kernel is a plugin's work on one sample, apart from the plugin: a struct
holding the state the plugin keeps between samples, and a `tick()` from
plain floats to a float. The plugin runs the kernel over its window; a
patch compiled to a program inlines it. What plays live and what a
compiled patch plays are then the same arithmetic.

`osc::fmop` is the first, and the pattern:

- **`plugins/<category>/<name>.h`** holds the kernel, `struct thk<Name>`.
  It includes `libthink/thKernel.h` and nothing else of the engine's: no
  `thArg`, no window, no `think.h`, so a program with no engine can
  include it.
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
  guards, and `thkSin2pi`, a sine taking its angle in turns, within
  2e-7 of `sin()`.
- **`scripts/kernelcheck`** holds each kernel against a double reference
  of the same formula, and the change to what the corpus renders is
  measured with `scripts/dspab -a OLD_PLUGINS -b NEW_PLUGINS` and said in
  the commit.

A kernel included by more than one plugin's source goes in the static
build's prelude, `plugins/thinkstatic.h`, as `thKernel.h` does: the
static build wraps each plugin in a namespace of its own, and a header's
guard would otherwise keep it out of every namespace but the first.
