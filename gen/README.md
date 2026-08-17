# The shipped pieces

Eleven `.gen` files, each of which is meant to be read as well as heard. Between
them they exercise every composer plugin in the tree and every ability the
`.gen` language has, and each one is built around a single idea rather than
around being impressive. The comment at the top of each file is the lesson;
this is the index.

Open one with the Composer window's **Open** button, press **Play**, and aim
the channels its header names at patches you like. The three timbre pieces
also move `amp`, the one chanarg every channel has, so they do something
audible whatever is loaded; their filter components need `amb01.dsp`. Channels are 1–16 here, the
same numbers the main window's patch tabs show.

Every piece is gated: `scripts/gencheck` loads all of them on every build, and
requires each one with a generator in it to deliver something inside a minute
of virtual time — a piece that loads and then says nothing is a piece with a
typo in it.

## Start here

| piece | the idea |
| --- | --- |
| [`airports.gen`](airports.gen) | **Free-running time.** Seven tape loops whose periods share no factor, so the piece never repeats. The original: `gen::eno_line`, a live `@density` knob, and a slow filter sweep the v1 format had no way to say. |
| [`pulse.gen`](pulse.gen) | **Clocked time.** The same machinery with every duration written in `beats`: change the tempo and four Euclidean patterns move together and stay locked. The other half of airports' argument — the clock lives in the value, not in the plugin. |
| [`loosen.gen`](loosen.gen) | **What a transformer does.** One Euclidean phrase, played straight, humanized, and then quantized, on three channels at once. Mute two and listen to each alone. |

## Playing it yourself

| piece | the idea |
| --- | --- |
| [`hands.gen`](hands.gen) | **Live input.** Nothing generates anything: every chain is fed by `input midi`, so hardware MIDI and the on-screen Keyboard (toolbar → **Kbd input**) drive an arpeggiator, a corrector, and a slow shadow. The piece that shows why `THC_EV_NOTEOFF` exists. |
| [`glider.gen`](glider.gen) | **A picture that is a control.** Conway's Game of Life, played. Double-click a stage on the canvas to fill the window with its board, then click cells while it runs — the next generation takes whatever you leave. **Capture to file** writes the board you made back into the piece. |

## Algorithms

| piece | the idea |
| --- | --- |
| [`fern.gen`](fern.gen) | **L-systems.** One grammar at three depths, three tempos and three octaves. Brackets push and pop time as well as degree, so a branch runs alongside what follows it and polyphony falls out of the grammar. |
| [`loom.gen`](loom.gen) | **Markov chains, and cellular automata.** A markov stage listens to an L-system in the same chain and paraphrases it with the teacher struck silent; underneath, rule 110 walks a sixteen-cell ring with the rule on a slider. |
| [`growth.gen`](growth.gen) | **Genetic algorithms over phrases.** `gen::evolve` plays its current champion each cycle and runs a generation while it sounds, so the piece *is* the search. |

## Timbre as material

| piece | the idea |
| --- | --- |
| [`weather.gen`](weather.gen) | **Generative timbre, plainly.** Four random walks pointed at knobs, over a pad of three lines. The walk emits a number and does not know where it lands; the sink names the target. Read its header before pointing a walk at something new — a chanarg's range belongs to the patch, and `amp` runs 0–127. |
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
`@knob` bindings on floats (airports, weather) and on whole numbers (pulse,
hands), `input midi` (hands), clicks on a
plugin's draw (glider), note sinks, named chanarg sinks (airports,
weather), the `chanarg = "*"` wildcard (tide, bloom), fan-out to several sinks
(weather, tide), pinned and unpinned seeds (all of them, both ways).

See [`../GEN_FORMAT.md`](../GEN_FORMAT.md) for the language and
[`../COMPOSITION_HANDOFF.md`](../COMPOSITION_HANDOFF.md) for why it is shaped
the way it is.
