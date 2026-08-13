# The `.dsp` and `.patch` formats

The DSP language is parsed by bison/flex (`libthink/thinklang.yy`,
`thinklex.ll`). It is a node-graph description language, which is why the node
editor maps onto it with essentially no impedance mismatch.

Most of this document exists because the node editor has to *write* these files
as well as read them, and writing found constraints that reading never revealed.
Anything that edits a `.dsp` needs the rules in section 3.

## 1. The DSP language

```
name "TS-1";
author "Leif Ames";

@cutoff = 4;              # a channel arg = user-facing knob
@cutoff.widget = 1;       # .min .max .label .widget .units .group
@cutoff.min = 0;
@cutoff.max = 16;

node freq misc::midi2freq {     # <name> <category>::<plugin>
    note = ionode->note;        # edge: another node's arg
};
node cutcalc math::mul {
    in0 = freq->out;
    in1 = @cutoff;              # edge: a channel arg
};
node ionode { out0 = mixer->out; channels = 2; play = env->play; };
io ionode;
```

There is **no valid empty `.dsp`** — `finishParse` rejects any file without an
io node. The smallest thing that loads is the info strings,
`node ionode { channels = 2; };` and `io ionode;`.

### Modulation is not a special case

The language draws no distinction between a constant parameter and a signal. A
node input can be bound to a literal, to a `@chanarg`, or to another node's
output, and the last of those is used constantly:

```
node map1 env::map {
    in     = env->out;          # envelope drives the mapping
    outmin = cutcalc->out;      # and the range is itself computed
};
node filt filt::res2pole2 {
    cutoff = map1->out;         # filter cutoff modulated by the envelope
};
```

That is parameter modulation, in the engine, today. Dragging a wire from an
oscillator's output onto an input that currently reads `@cutoff` changes that
arg from `ARG_CHANNEL` to `ARG_POINTER`, which the parser, the graph builder and
the audio path all already handle.

The one thing genuinely **not** supported is a `@chanarg` being driven by a
node. Chanargs are declared `@x = <constant>` and written by the GUI and MIDI
controllers, and nothing can drive one from the graph. So "this parameter varies
with an LFO" needs no engine work and "the knob moves on its own" does.

### Chanargs and controls

All 206 chanarg declarations in the corpus carry `.widget = 1`, `.min` and
`.max`; 173 also give `.label`. So declaring a chanarg *is* declaring a control.
But the parser also stores `name`, `author` and `description` as chanargs — 110
of them — and those are strings rather than knobs. `.widget` is the line the
format already draws between the two: **316 chanargs, 206 controls.**

`.group` is metadata the engine never reads. It exists so an editor can draw an
envelope's four sliders as one titled block rather than four unrelated rows:

```
@a = 5 ms;
@a.widget = 1;
@a.min = 0;
@a.max = 2000ms;
@a.label = "Attack";
@a.group = "Envelope";
```

It sits alongside `.label` and `.units` in the grammar — one more case in the
rule that already handles string metadata — and `thArg` carries it.

### The io node

The io node is the one node with no plugin, so nothing declares the direction of
its args. It is both the MIDI source (`note`, `velocity`, `trigger` are read by
other nodes) and the audio sink (`out0`, `out1`, `play` are written to it), and
any tool that treats it as one vertex will see a cycle in 89 of the 92 shipped
files that is not really there.

Direction can be recovered, because the engine's own use of the io node is
narrow. `thMidiChan::process()` reads exactly three things off it: `OUTPUTPREFIX`
plus a channel digit for the audio it mixes, `play` to learn the note has ended,
and `channels` to size the mix. Everything else travels the other way —
`thMidiNote` writes note, velocity and trigger, `thMidiChan` creates amp, and the
author's constants are read by whoever wants them.

So an arg is an input to the audio-out half if

- the engine reads it — `out<N>`, `play`, `channels`; or
- **this file wires something into it.**

The second clause is not decoration. 23 args across the corpus are written by a
node and read back by others — the io node used as a relay. A name-only rule
would silently drop those wires. The rule is spelled with `OUTPUTPREFIX` and
`TH_MAX_CHANNELS` rather than a literal list so it cannot drift from the engine
it describes.

Authors also park patch constants in the io block: `res = 0.3` sits there and
forty other nodes read `ionode->res`. Args with no port on either side — a dozen
dead constants, mostly typos like `inwav` for `inwave` — belong to the source
half, where a value the io node offers belongs even when nothing takes it up.

## 2. The `.patch` format

A `.patch` is **not** a graph. It is a reference to a `.dsp` plus flat
overrides — a preset over that DSP's `@chanargs`:

```
dsp ts1.dsp
info author Leif Ames
info title Phat Rip
cutoff 8.809662
res 2.854232
```

`src/gui/ArgTable.cpp` renders these as sliders.

## 3. Writing a `.dsp`

### Splice, do not re-emit

The parser throws away things a naive re-emit would not restore:

- **Comments.** 59 of 92 files have them, 391 in total, and they are the
  author's notes. Losing them on first save would be vandalism.
- **Units.** 283 values are written `5 ms` or `90%`. The lexer folds these to
  raw floats, so `a = 5 ms` comes back as `a = 220.5` — correct, unreadable.
- **Synthesised args.** `buildArgMap()` calls `setArg(name, 0)` for every arg a
  plugin registered but the `.dsp` did not mention. Re-emitting the in-memory
  model would write out dozens of `reset = 0;` lines nobody authored.
- **Arithmetic.** Only 8 right-hand sides across the corpus, but the same
  problem.

So `NodeLayout` and `NodeEdit` splice into the source text and never reconstruct
from the parsed model. `NodeLayout` reads the file, drops its own `# @layout`
lines, copies everything else through untouched and appends a fresh block.

Two things that only showed up in practice:

- **A generated block has to be idempotent.** Anchor the whole block, prose
  included, on one prefix — otherwise its own header comments do not match the
  prefix being stripped and every save adds two more lines.
- **Scramble before checking a round trip.** Asserting that positions survive is
  worthless if the test writes out the positions `layout()` just computed, since
  a save that silently did nothing still passes.

### What the grammar allows

- The lexer's number pattern is `[0-9]+(\.[0-9]*)?`. **No exponent.** A value
  that needs one cannot be written at all; `NodeEdit` refuses rather than
  emitting something that will not parse.
- **Negatives** exist only as a unary-minus rule over that. Eight in the corpus.
- `5 ms` is `5 * TH_SAMPLE / 1000` and `50%` is `50 * TH_MAX / 100`. Both are
  exactly invertible, so a value written with a unit keeps it.
- **229 uses of `th_max` and `th_min`**, plus `th_range`, `th_midimax` and
  `th_sample`. A writer that did not recognise these would turn
  `inmax = th_max` into `inmax = 1` on the first save of any file containing one.
- **Arithmetic right-hand sides are refused.** An editor that silently replaced
  someone's `a * 2` with a constant would be doing exactly the damage splicing
  exists to prevent.
- **A label cannot contain a quote.** The lexer's string is `"[^"\n]*"` with no
  escapes at all, so there is no spelling for one.
- **`@x.min` before `@x` has nothing to modify** — the parser says so and
  ignores it. A control block is written value, widget, min, max, label, in that
  order, before the first `node`, which is where all 206 of the shipped ones sit.
- **A unit is distinguished by a number in front of it**, not by a non-word
  character before it. `80ms` (33 occurrences) and `5 ms` (65) are both units.
  The original spacing is preserved: `80ms` stays `80ms`.
- **A node feeding itself is legal.** `jp420`, `organ2` and `jp420-B` all
  contain `ionode.fade78 = ionode->velocity`. The io node is one node in the file
  and two boxes on screen, so a "no self-edges" rule only makes sense phrased
  over boxes — which is why it lives in `NodeGraph::canConnect` and not in the
  writer.
- **A control source is spelled `@blim`**, not `blim->blim`. That is
  `NodeEdit::connectControl`, separate from `connect` rather than inferred from
  a name starting with `@`.

### Disconnecting

`disconnect` rewrites the line to `= 0` rather than deleting it. To the engine
the two are identical, but only the rewrite keeps the line's position,
indentation and trailing comment — and only the rewrite makes a reconnect
restore the file byte for byte. All 3476 connections in the corpus are spelled
`name->port` with no spaces, so rewriting one reproduces the original text
exactly.

Deleting a node or a control also has to rewrite every reference to it. Left
alone those make the file load with `setPointers: Node x not found!!` and read
zero — a delete that quietly breaks three other nodes is worse than one that
says it disconnected them.

### Exact float equality does not work

libthink can be built with `-ffast-math`, which lets the compiler turn the
grammar's `× TH_SAMPLE / 1000` into a multiply by the reciprocal. So `0.5 ms` is
held as 22.0500011 where honest arithmetic gives 22.0499992 — a couple of ULP
apart, and enough that re-deriving the literal exactly is impossible. Demanding
it rewrote `0.5 ms` as `0.50000003 ms`. The comparison is four ULP wide.

### The guarantee is about untouched values, not round trips

Once a value has genuinely been changed and changed back, `th_max` comes back as
`1`. The writer has no memory of how a number used to be spelled, only of when
it does not need to touch one at all. So the property `scripts/dspwrite` asserts
is: **a write of the value already there changes no byte of the file.**

## 4. Structured comments

Two things are stored as comments so that files stay loadable by the current
parser and by every existing tool:

```
# @layout freq 120 40          node position
# @probe filt out spectrum     an armed visualizer
```

Both are invisible to everything that is not the editor. See
[NODE_EDITOR.md](NODE_EDITOR.md) and [VISUALIZERS.md](VISUALIZERS.md).

## 5. Known-bad files

**11 of the 92 shipped DSPs do not load at all.** They reference `input/wav`,
`input/alsa` or `misc/wlan` — plugins that compile but are deliberately not in
the build, `wav.cpp`'s own description string being `"Wav Input (BROKEN)"`.

**All 101 patches load.** Two of them did not until recently:
`patches/pads/Rythmic.patch` and `Rythmic-2.patch` named an absolute
`/usr/local/share//thinksynth/dsp/mfm03.dsp` that was never in the tree, and
now name `mfm01.dsp`, which declares exactly the chanargs they set.

The CI gates therefore run over 81 DSPs and all 101 patches.
`cmake/RunHarness.cmake` filters DSPs by what a file *references* rather than
by name, so the exclusion cannot go stale. The patch side once filtered by
name, on `Rythmic` — which also caught `ThickRythmic.patch` and
`ThickRythmic-2.patch`, two healthy patches on `ts2.dsp`, and dropped them from
the gate for as long as it stood. That is the argument for matching on content
in one line.
