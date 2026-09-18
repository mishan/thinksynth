# cairo2d

cairo's API, recorded and replayed on a Canvas2D.

This is not cairo, and it is not a binding to one. It is a `cairo.h` and a
`cairomm/context.h` that answer to cairo's names for the corner of cairo
that drawing a diagram uses -- paths, fills, strokes, the toy text API, a
transform, a clip, and one image surface -- and record every call into a
flat list. A hundred and fifty lines of JavaScript then replay that list
onto a `CanvasRenderingContext2D`, whose vocabulary is the same one.

It exists so that C or C++ which draws through cairo on a desktop can be
compiled to WebAssembly and draw the same picture in a browser, with its
include path changed and nothing else.

## Why not one of the other answers

- **Real cairo, compiled to wasm.** It works, and it brings pixman and
  freetype: about a megabyte of rasteriser to draw rectangles the browser
  will rasterise anyway, and a font stack beside the one already in the
  page.
- **Draw it again in JavaScript.** Two versions of every picture a person
  looks at, kept in step by hand for ever.
- **A C binding to the Canvas API.** Several exist, and they are the
  right answer for new code. They are no help at all to code that already
  says `cairo_move_to`.

What is here is the fourth thing: cairo's own spelling, for code that has
already been written once.

## What is in it

| | |
|---|---|
| `cairo.h` | the C API, and the whole of what a caller may use |
| `cairomm/context.h` | the same, as cairomm spells it, for C++ |
| `cairo2d.h` | the host's side: make a recorder, read the list |
| `cairo2d.cpp` | the recorder |
| `replay.js` | the replayer, an ES module |
| `test/` | both halves, and the check that they still agree |

About a thousand lines, all told.

## Using it

```c
#include <cairo2d.h>

cairo_t *cr = cairo2d_create();

cairo2d_begin(cr);              /* a frame */
draw_whatever(cr, width, height);
```

and then, from JavaScript, with the three tables read out of the module's
heap:

```js
import { replay } from './replay.js';

replay(ctx, ops, strings, surfaces, { width, height, dpr });
```

Where the tables come from is the host's business: through Emscripten,
`cairo2d_ops()` is a pointer into `HEAPF32` and `cairo2d_string()` a
pointer into the heap, and reading them is a `subarray` and a
`UTF8ToString`.

## The list

One flat array of `float`. A record is an opcode followed by exactly as
many operands as `cairo2d_op_arity()` says, and nothing else -- no length
prefix, because the arity table is the contract. The one variable-length
op, `SET_DASH`, says its own count first.

Strings and image surfaces are **indices into side tables**, never
pointers: a `float` holds integers exactly only up to 2^24, and a heap
pointer above sixteen megabytes would land silently on the wrong byte.

The tables and the list live until the next `cairo2d_begin()`. A surface
the drawing code destroyed is kept alive by the list that refers to it,
because a caller which computes pixels, paints them and frees them inside
one frame -- which is what a spectrogram does -- must not leave the
replayer reading freed memory.

## Where the two models differ

Three places, and the replayer earns its keep in all three:

1. **A path survives a fill.** cairo's `fill` and `stroke` clear the
   current path; Canvas2D's do not. So a `beginPath()` is owed after
   either, and paid the next time a path op arrives. The `_preserve` pair
   owe nothing.
2. **`new_sub_path`.** Canvas2D has no such call, so the replayer moves to
   the following arc's first point instead -- which is what cairo does
   internally, and it is why a rounded rectangle drawn out of four arcs
   comes out with no stray line across it.
3. **`paint`.** With a colour it is a `fillRect` over the whole surface
   under the base transform, which the clip cuts down to the region cairo
   would have painted. With a surface it is a `drawImage` at the source's
   origin under the current transform.

## Text

`cairo_text_extents` is the one call that needs an answer while the list
is being built: a label is centred and truncated from its width. The
recorder composes one CSS font string -- `italic bold 12px sans-serif` --
and that same string is what the measurement is made with and what goes
into the list for the replayer to assign to `ctx.font`. One string, one
place, so a label cannot be laid out in a font other than the one it is
drawn in.

Where the measurement comes from is the host's: `cairo2d_set_measure()`
takes a function. Under Emscripten the default asks the browser for
`measureText` on an offscreen 2D context, synchronously, which works on
the main thread and in a worker. Where there is no canvas at all -- Node,
a native test, an audio worklet -- there is a built-in estimate from the
font size and the length, and a label laid out from it is a pixel or two
out of centre.

## What it will not do

Everything not in `cairo.h`. A program that reaches for
`cairo_set_line_join`, a gradient, a PDF surface or Pango does not
compile, in the line that reached, rather than linking and drawing
something wrong. The list is the contract; it grows when a caller needs
it to, and never by accident.

Also absent, on purpose: any include of anything outside this directory.

## The test

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

`test/cairo2dtest.cpp` checks that each call records what it says it does,
that both faces record the same list for the same drawing, that the list
can be walked with nothing but the arity table, that text is laid out with
the metrics the host gave and drawn at the current point, and that a
surface's pixels outlive the caller's reference.

`test/replaytest.mjs` reads the op table out of the built program and
holds `replay.js` against it, name by name and arity by arity -- the
opcodes are written out twice, in two languages, and the day they part is
the day every picture after the changed op is drawn from operands read as
opcodes. Then it replays a recorded list onto a context that writes down
what it was asked to do, and checks the three places above.

## Licence

Public domain, or CC0 where that is not a thing. Take it.
