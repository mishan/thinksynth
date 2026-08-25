# The shipped pieces

Twelve `.gen` files, each of which is meant to be read as well as heard. Between
them they exercise every composer plugin in the tree and every ability the
`.gen` language has, and each one is built around a single idea rather than
around being impressive. The comment at the top of each file is the lesson;
this is the index.

Open one from the Composer window's menu (**☰ → Open**) and press **Play**.

`airports.gen`, `weather.gen` and `breath.gen` need nothing else: they carry
their own instruments. An `instrument` block names a `.dsp` and the chanarg values that
make it *this* instrument, a sink binds to the name, and the loader puts it on
a channel and loads it for you — one file you can send somebody. A piece knob
can reach in there too, so one slider drives a composer and an instrument at
once. See §4b of [`../GEN_FORMAT.md`](../GEN_FORMAT.md), and `UNIFICATION.md`
for where this is going.

The rest still name channels, so aim the ones each header lists at patches you
like before pressing Play. The two remaining timbre pieces move `amp`, the one
chanarg every channel has, so they do something audible whatever is loaded;
their filter components need `amb01.dsp`. Channels are 1–16 here, the same
numbers the main window's patch tabs show.

## The window

The canvas is the piece. Chains read left to right, one row each; the knobs a
piece declares sit in a lane across the top with a wire down to every param
they drive. Everything is reachable from there:

- **Drag a knob node's track** to move it while the piece plays. The file
  remembers where you let go.
- **Drag from a knob's port** — the dot on its bottom edge — onto a stage box
  to bind it. The drop asks which param, because a stage with six numbers is
  six honest answers. Unbinding is in the Selection tab, next to the param it
  releases.
- **Press the handle** in a stage box's title bar (the three little sliders)
  for that stage's parameters, with units and knob bindings.
- **Double-click a stage** that draws something to fill the canvas with it, and
  again — or Escape — to put it back. `glider.gen` is the piece to try this on.
- **Drag a stage box** sideways to reorder its chain.
- **Ctrl+wheel** zooms.

**Edit** opens a panel with two tabs: *Piece* for the things the whole file
declares — name, seed, tempo, knobs, scales, presets — and *Selection* for
whatever is selected on the canvas, which clicking raises for you. New, Open,
Save and Revert are in the menu; **☰ → Piano roll** puts the roll away when
the canvas wants the whole window.

Every piece is gated: `scripts/gencheck` loads all of them on every build, and
requires each one with a generator in it to deliver something inside a minute
of virtual time — a piece that loads and then says nothing is a piece with a
typo in it.

## Start here

| piece | the idea |
| --- | --- |
| [`airports.gen`](airports.gen) | **Free-running time**, and **a piece that carries its instrument.** Seven tape loops whose periods share no factor, so the piece never repeats. The original: `gen::eno_line`, a live `@density` knob, and a slow filter sweep the v1 format had no way to say — and now an `instrument` block, so it is the one piece here that needs no setting up at all. |
| [`pulse.gen`](pulse.gen) | **Clocked time.** The same machinery with every duration written in `beats`: change the tempo and four Euclidean patterns move together and stay locked. The other half of airports' argument — the clock lives in the value, not in the plugin. |
| [`loosen.gen`](loosen.gen) | **What a transformer does.** One Euclidean phrase, played straight, humanized, and then quantized, on three channels at once. Mute two and listen to each alone. |

## Playing it yourself

| piece | the idea |
| --- | --- |
| [`hands.gen`](hands.gen) | **Live input.** Nothing generates anything: every chain is fed by `input midi`, so hardware MIDI and the on-screen Keyboard (title bar → **Kbd input**) drive an arpeggiator, a corrector, and a slow shadow. The piece that shows why `THC_EV_NOTEOFF` exists. |
| [`glider.gen`](glider.gen) | **A picture that is a control.** Conway's Game of Life, played. Double-click a stage on the canvas to fill the window with its board, then click cells while it runs — the next generation takes whatever you leave. **Capture to file** writes the board you made back into the piece. |

## Algorithms

| piece | the idea |
| --- | --- |
| [`fern.gen`](fern.gen) | **L-systems.** One grammar at three depths, three tempos and three octaves. Brackets push and pop time as well as degree, so a branch runs alongside what follows it and polyphony falls out of the grammar. |
| [`loom.gen`](loom.gen) | **Markov chains, and cellular automata.** A markov stage listens to an L-system in the same chain and paraphrases it with the teacher struck silent; underneath, rule 110 walks a sixteen-cell ring with the rule on a slider. |
| [`growth.gen`](growth.gen) | **Genetic algorithms over phrases.** `gen::evolve` plays its current champion each cycle and runs a generation while it sounds, so the piece *is* the search. |
| [`breath.gen`](breath.gen) | **DSP modules composing.** An `osc::simple` and an `env::adsr` in the chains, running fifty times a second on the composer's side — the same plugins a patch is built from, wired with the same `->`, modulating the music instead of the audio. One breathes a line's density over twenty seconds; the other shapes another over ten minutes. |

## Timbre as material

| piece | the idea |
| --- | --- |
| [`weather.gen`](weather.gen) | **Generative timbre, plainly** — and **one knob, both worlds.** Four random walks pointed at knobs, over a pad of three lines: the walk emits a number and does not know where it lands, and the sink names the target. It carries that pad now, so `Breadth` drives a stage's density and two of the instrument's own chanargs from one slider, and `Tail` sets the pad's release in milliseconds. Read its header before pointing a walk at something new — a chanarg's range belongs to the patch, and `amp` runs 0–127. |
| [`tide.gen`](tide.gen) | **Presets, and the line between two.** `gen::morph` travels between two named chanarg vectors — as a generator on its own clock, and as a transformer where each note schedules its own sweep. |
| [`bloom.gen`](bloom.gen) | **Genetic algorithms over timbre.** `gen::breed` searches the corridor the piece's own presets declare. A component neither preset names cannot be invented, which is the reach limit stated as arithmetic. |

## What each piece covers

Plugins: `eno_line` (airports, weather), `euclid` (pulse, loosen, tide),
`quantize` (airports, hands, loosen), `humanize` (loosen, fern), `walk`
(airports, weather), `arp` (hands), `lsystem` (fern, growth, loom), `markov`
(loom), `ca` (loom), `life` (glider), `evolve` (growth), `morph` (tide),
`breed` (bloom).

Language: `tempo` and `beats` (pulse), free-running seconds (airports,
weather), `scale` (airports, hands, loosen, weather), `preset` (tide, bloom),
`instrument` blocks and sinks bound by name (airports, weather), knobs bound
into an instrument (weather), dsp nodes as chain stages and `->` bindings
(breath),
`@knob` bindings on floats (airports, weather) and on whole numbers (pulse,
hands), `input midi` (hands), clicks on a
plugin's draw (glider), note sinks, named chanarg sinks (airports,
weather), the `chanarg = "*"` wildcard (tide, bloom), fan-out to several sinks
(weather, tide), pinned and unpinned seeds (all of them, both ways).

See [`../GEN_FORMAT.md`](../GEN_FORMAT.md) for the language and
[`../COMPOSITION_HANDOFF.md`](../COMPOSITION_HANDOFF.md) for why it is shaped
the way it is.
