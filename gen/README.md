# The shipped pieces

Twenty-four `.gen` files, each of which is meant to be read as well as heard.
Fourteen of them exercise every composer plugin in the tree and every ability
the `.gen` language has, each built around a single idea rather than around
being impressive; the other ten are pieces first and lessons second. The comment at the top of each file is the lesson;
this is the index.

Open one from the Composer window's menu (**☰ → Open**) and press **Play**.

`airports.gen`, `weather.gen`, `breath.gen`, `reshape.gen`, `colony.gen`,
`ebb.gen`, `round.gen`, `orrery.gen`, `overworld.gen`, `cavern.gen`, `boss.gen`,
`attract.gen`, `village.gen`, `invention.gen` and `belfry.gen` need nothing
else: they carry their own instruments. An `instrument` block names a `.dsp` and the chanarg values that
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
| [`reshape.gen`](reshape.gen) | **The composer changes the instrument.** Two chains emit no pitches: one rebuilds the channel around a different instrument every forty seconds, the other reaches inside whatever is there and moves a constant the patch never declared. The notes never change — everything you hear moving is the instrument underneath them. |
| [`colony.gen`](colony.gen) | **A chain is a pipeline.** Four stages, each hearing only the one before it: a Euclidean rhythm draws itself onto Conway's board, a markov learns the colony that grows and paraphrases it, and a harmonizer answers every note with a chord counted in scale degrees. The rhythm is never heard — its notes become *cells*, so everything you do hear is a consequence. Silence the line and the board plays three pitches forever. Underneath, `gen::swap` and `gen::reshape` rebuild the instrument while none of that changes. |
| [`breath.gen`](breath.gen) | **DSP modules composing.** An `osc::simple` and an `env::adsr` in the chains, running fifty times a second on the composer's side — the same plugins a patch is built from, wired with the same `->`, modulating the music instead of the audio. One breathes a line's density over twenty seconds; the other shapes another over ten minutes. |

## Timbre as material

| piece | the idea |
| --- | --- |
| [`weather.gen`](weather.gen) | **Generative timbre, plainly** — and **one knob, both worlds.** Four random walks pointed at knobs, over a pad of three lines: the walk emits a number and does not know where it lands, and the sink names the target. It carries that pad now, so `Breadth` drives a stage's density and two of the instrument's own chanargs from one slider, and `Tail` sets the pad's release in milliseconds. Read its header before pointing a walk at something new — a chanarg's range belongs to the patch, and `amp` runs 0–127. |
| [`tide.gen`](tide.gen) | **Presets, and the line between two.** `gen::morph` travels between two named chanarg vectors — as a generator on its own clock, and as a transformer where each note schedules its own sweep. |
| [`bloom.gen`](bloom.gen) | **Genetic algorithms over timbre.** `gen::breed` searches the corridor the piece's own presets declare. A component neither preset names cannot be invented, which is the reach limit stated as arithmetic. |

## Pieces

Four files where the idea is the music and the mechanism is in service of
it. Each carries its own instruments; each header says how it is put
together, in the same detail as the others.

| piece | the idea |
| --- | --- |
| [`ebb.gen`](ebb.gen) | **Two keys and the tide between them.** A C pentatonic cloud that never changes, over a ground that does: two bass chains, one on A and one on F, each with its own control-rate sine, wired so one is empty where the other is full. The piece turns from A minor to F lydian and back on a cycle the `Tide` knob sets, with nothing transposed — only the root moves. One upper voice takes a side and plays the lydian fourth on the F side alone. |
| [`round.gen`](round.gen) | **A canon on one grammar.** One L-system subject, three entries: straight, six steps later a fifth up, and at half speed an octave and a half down. The lag is written as rests in the axiom, the augmentation as `math::mul` on the `Pace` knob, and the ladders are pentatonic so every lag is consonant. Take the trailing rests off the lead and it becomes a phase piece. |
| [`orrery.gen`](orrery.gen) | **Gears on one clock, and a bass that reads the chords.** Four Euclidean rings of different sizes in `beats`, a harmonizer spelling the chords by degree, a genetic lead on the `Lift` knob, and two voices swapped under it every thirty-two bars. The bass follows the progression without a message passing between chains: its pool is one four-bar phrase long, five notes under each chord, and the ring is the index. |
| [`invention.gen`](invention.gen) | **Two voices on a Moog, and the ornaments are rules.** In the spirit of Wendy Carlos: a two-part invention in D minor, a saw lead and a square bass each on their own patch, a harpsichord of broken chords from a pool, and for once an L-system's rules are not empty -- `M`, `T` and `N` in the axiom are rewritten into a mordent, a trill and a turn on whatever note the turtle is standing on. The alto reads the same axiom with the ornaments spelled plain, through `xform::counterpoint`, every other time round, on a `Third` fader. |

## Game music

Six pieces in the idioms of the consoles and cabinets, which were generative
music before the word: a few voices, a loop, and a chip's tricks for making
them sound like more. Each is written, where it is written, as a depth-0
L-system -- an axiom that nothing rewrites is a phrase said one step at a
time, with rests, and with `_` tying a note across the steps after it. Each
carries its own instruments.

| piece | the idea |
| --- | --- |
| [`overworld.gen`](overworld.gen) | **A console's voices, and the echo.** Two pulses, an arpeggio pulse, a triangle and noise, at a hundred and fifty. The tune is one grammar in sixteenths with ties; the second pulse plays only its echo, a dotted eighth behind, through `xform::echo` on the `Echo` knob. The arpeggio is a sixteen-step ring over a hundred-and-twenty-eight-note pool, and the `Duty` knob is the lead's pulse width, reaching into the instrument. |
| [`cavern.gen`](cavern.gen) | **Seven to the bar, and water that keeps no time.** An ostinato and a heartbeat on a seven-step ring, a tune in seven-eight on the phrygian ladder, and a markov that hears the tune with the teacher silenced and dreams it on a bell. Over all of it, `gen::ca` running rule 30 in *seconds*, on the `Drip` knob: the machine keeps seven, the cave keeps none. |
| [`boss.gen`](boss.gen) | **A riff that never moves.** Two bars of sixteenths on E, a tresillo of stabs spelled by `xform::harmonize` from E phrygian, a solo bred by `gen::evolve` on the `Fury` knob, gated by `xform::form` to three phrases in four and rebuilt around the other of two voices every sixteen bars, and a gear of seven sixteenths turning against the four. The hats go through `xform::chance` and `xform::ratchet`. |
| [`attract.gen`](attract.gen) | **One voice pretending to be a chord.** The arpeggio-chord trick: a sixty-four-step ring of thirty-second notes over a hundred-and-twenty-eight-note pool, one voice sounding a chord a bar. `gen::morph` sweeps the chip's filter between two presets and back through a `*` sink, a walk moves its pulse width, and the tune slides into every note on the `Glide` knob. |
| [`belfry.gen`](belfry.gen) | **A castle at night.** Harmonic minor with its leading tone, sixteenth arpeggios from a pool, an organ whose triads are three rings in lockstep because a G and a G sharp cannot both be spelled from one scale, a lead with a mordent and a cadence trill written as rules, and a galloping kick. The second pulse is the lead's echo for four bars and the lead in thirds for four, two chains on one instrument through `xform::form`. |
| [`village.gen`](village.gen) | **Chords that behave, and a second voice that argues.** A town theme with a shuffle. `gen::progression` walks the chords of G major with a cadence every four, spelled by `harmonize` on one chain and played by `xform::bassline` on another that shares its `seed`, so the two agree without a message. A pentatonic tune, and `xform::counterpoint` hearing the same grammar and putting a first-species voice under it. `xform::swing` on the `Shuffle` knob, `xform::form` for the intro and for the bars the flute sits out, `chance`, `ratchet` and `level` on the brushes. |

## What each piece covers

Plugins: `eno_line` (airports, weather), `euclid` (pulse, loosen, tide),
`quantize` (airports, hands, loosen), `humanize` (loosen, fern), `walk`
(airports, weather), `arp` (hands), `lsystem` (fern, growth, loom), `markov`
(loom), `ca` (loom), `life` (glider), `evolve` (growth), `morph` (tide),
`breed` (bloom), `swap` and `reshape` (reshape), `harmonize` (colony).
invention uses `lsystem` rules as ornaments and `counterpoint`, `level`
and `form` on its third voice. The game pieces use `lsystem` at depth 0 as
a sequencer, with ties (all six), `euclid` as one with its pool as the
progression (all six), `ca` and `markov` in seconds against a clocked bar
(cavern), `evolve` and `swap` (boss), `morph` through a `*` sink
(attract), `echo` (overworld), `form` (boss, village), `chance` and
`ratchet` (boss, village), `swing`, `level`, `progression`, `bassline`
and `counterpoint` (village).
`life` appears twice and differently: `glider.gen` plays it, `colony.gen`
feeds it.

Language: `tempo` and `beats` (pulse), free-running seconds (airports,
weather), `scale` (airports, hands, loosen, weather), `preset` (tide, bloom),
`instrument` blocks and sinks bound by name (airports, weather), knobs bound
into an instrument (weather), dsp nodes as chain stages and `->` bindings
(breath), structure edits -- swaps and node constants (reshape, colony),
a generator's output drawn onto another stage's state (colony),
`@knob` bindings on floats (airports, weather) and on whole numbers (pulse,
hands), `input midi` (hands), clicks on a
plugin's draw (glider), note sinks, named chanarg sinks (airports,
weather), the `chanarg = "*"` wildcard (tide, bloom), fan-out to several sinks
(weather, tide), pinned and unpinned seeds (all of them, both ways).

See [`../GEN_FORMAT.md`](../GEN_FORMAT.md) for the language and
[`../COMPOSITION_HANDOFF.md`](../COMPOSITION_HANDOFF.md) for why it is shaped
the way it is.
