# The shipped pieces

Thirty-one `.gen` files, each of which is meant to be read as well as heard.
Fourteen of them exercise every composer plugin in the tree and every ability
the `.gen` language has, each built around a single idea rather than around
being impressive; the other seventeen are pieces first and lessons second. The
comment at the top of each file is the lesson; this is the index.

Open one from the Composer window's menu (**☰ → Open**) and press **Play**.

**The sections below are in the files.** Each piece declares
`category "Game music";` beside its `name`, and that is what the Composer's
Open and the page's piece menu group by -- so this index and the menus say the
same thing because they are the same fact, rather than because somebody kept
them in step. They were not in step: three pieces had drifted out of every
section here before the field existed. `scripts/gencheck` now fails a shipped
piece that is filed under none of the eight or under something else; a piece of
your own may say whatever it likes, and one that says nothing lands under
Uncategorized. See [`../docs/GEN_FORMAT.md`](../docs/GEN_FORMAT.md) § 7a.

`airports.gen`, `weather.gen`, `breath.gen`, `reshape.gen`, `colony.gen`,
`ebb.gen`, `round.gen`, `orrery.gen`, `overworld.gen`, `cavern.gen`, `boss.gen`,
`attract.gen`, `village.gen`, `invention.gen`, `belfry.gen`, `warehouse.gen`,
`anthem.gen`, `acetate.gen`, `boombox.gen`, `outrun.gen`, `pearl.gen` and
`riviera.gen` need nothing else:
they carry their own instruments. An `instrument` block names a `.dsp` and the chanarg values that
make it *this* instrument, a sink binds to the name, and the loader puts it on
a channel and loads it for you — one file you can send somebody. A piece knob
can reach in there too, so one slider drives a composer and an instrument at
once. See [`../docs/GEN_FORMAT.md`](../docs/GEN_FORMAT.md), and `../docs/UNIFICATION.md`
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
| [`orrery.gen`](orrery.gen) | **Gears on one clock, and a bass that reads the chords.** Four Euclidean rings of different sizes in `beats`, a harmonizer spelling the chords by degree and voicing each against the one before it, a genetic lead on the `Lift` knob, and two voices swapped under it every thirty-two bars. The bass follows the progression without a message passing between chains: its pool is one four-bar phrase long, five notes under each chord, and the ring is the index. |
| [`invention.gen`](invention.gen) | **Two voices on a Moog, a chorus on the way out, and the ornaments are rules.** In the spirit of Wendy Carlos: a two-part invention in D minor, a saw lead and a square bass each on their own patch, a harpsichord of broken chords from a pool, and for once an L-system's rules are not empty -- `M`, `T` and `N` in the axiom are rewritten into a mordent, a trill and a turn on whatever note the turtle is standing on. The alto reads the same axiom with the ornaments spelled plain, through `xform::counterpoint`, every other time round, on a `Third` fader. |

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
| [`village.gen`](village.gen) | **Chords that behave, and a second voice that argues.** A town theme with a shuffle. `gen::progression` walks the chords of G major with a cadence every four, spelled by `harmonize` on one chain -- voice-led, so the comping hand stays where it is -- and played by `xform::bassline` on another that shares its `seed`, so the two agree without a message. A pentatonic tune, and `xform::counterpoint` hearing the same grammar and putting a first-species voice under it. `xform::swing` on the `Shuffle` knob, `xform::form` for the intro and for the bars the flute sits out, `chance`, `ratchet` and `level` on the brushes. |

## The floor

Two pieces for a room with a kick in it, and the plugins and graphs the room
turned out to need: a monophonic bass that slides (`mono = 1` on the io node
and a slew on the frequency), effect graphs on a channel's sum for the delay
throw and the reverb tail, and three composers -- `gen::steps` for a row of
values to a knob, `gen::pump` for the sidechain a channel cannot hear, and
`xform::transpose` for the octave down. They are also the first two pieces
with an arrangement rather than a texture: a list of `section` statements at
the top saying, in order, how long each stretch of the piece is and which
chains it mutes or leans on, and `section end` so the piece stops.

And the two that stop a written line repeating itself exactly: `xform::vary`,
which does one of six things to each note -- leaves it out, takes it an octave
away, pushes it off the grid, says it twice, leans into it from the scale, or
flicks a mordent on it -- and `xform::accent`, which weights a note by where it
falls, over a pattern and across a bar. `gen::euclid` answers three bars with a
fourth from its `fill` pool. The filter envelopes in `supersaw`, `stab`, `bass`
and `ladder` are scaled by velocity, so all of that is heard as tone and not
only as level; `pluck` already was.

| piece | the idea |
| --- | --- |
| [`warehouse.gen`](warehouse.gen) | **Techno.** A bass line in sixteenths whose `hold` is longer than its `step`, so adjacent notes slide on a one-voice instrument and a rest is a fresh attack; `gen::steps` walking the bass filter's cutoff through eight values, and a row of accents on the hat's `amp`; stabs from a ring with rests in its pool into `fx/echo.dsp` on their channel, answered every fourth bar by the ring's `fill` pool; `xform::accent` weighting the bass, which its filter hears as brightness; a supersaw pad in `fx/hall.dsp` with `gen::pump` ducking it under every kick on the `Pump` knob; and sixty-four bars of `section` at the top saying where the piece goes, over `fx/limiter.dsp` on the mix -- a top-level `effect` statement, which is the one place a limiter can be. |
| [`anthem.gen`](anthem.gen) | **Trance.** A chord a bar on a supersaw pad, voice-led so Am F C G moves rather than climbs, chopped by a sixteenth-note gate from `gen::steps`; plucks arpeggiating the chord tones into a delay, pumped by `gen::pump`, with every eighth bar coming from the ring's `fill` pool; a rolling offbeat bass and a second copy an octave down through `xform::transpose` that the arrangement swaps in for the breakdown; a lead whose filter climbs over eight bars and drops, by `gen::morph` looping between two presets, and which `xform::vary` leans into, ornaments and pushes off the grid; `xform::accent` on the hats; the `Width` knob reaching into two supersaws at once. |

## The eighties

Five pieces on the instruments the decade is made of, which the tree did not
have until recently: a phase-modulation operator (`osc::fmop`) and the DX
graphs on it, a sampler (`osc::sample`) and a kit of this repository's own
drums rendered to wavs, a string machine, a Juno pad, a clav, a vocoder, a
flanger and a gated reverb. Each carries its own instruments and its own
arrangement.

| piece | the idea |
| --- | --- |
| [`pearl.gen`](pearl.gen) | **Synthpop.** One chord walk heard three times at once: three chains run `gen::progression` with the same `seed`, and a DX electric piano voice-leads it, a Juno pad spells it wider, and `xform::bassline` puts a two-operator bass under it. The piano's velocity goes to its modulator's index rather than its output, so `harmonize`'s taper makes the inner voices duller and not just quieter. The kit is `osc::sample` on one channel with the note number picking the drum, and the snare is a *second* channel of the same kit, because `fx/gate.dsp` has to be on the snare alone and a channel has one effect. `xform::swing` a quarter of the way to a triplet, on the hats and the bass. |
| [`acetate.gen`](acetate.gen) | **A kit that is recordings, and an orchestra that is one note.** Nothing in it is synthesized at the moment you hear it: every drum is a recording of one of this tree's own drum graphs played back by `osc::sample` -- a machine playing short recordings, which is a LinnDrum -- and the chord answering the turnaround is one recording of `stab` and `brass` at C4, transposed by key, which is a Fairlight. The hit gets shorter as it climbs, because a sample read at `freq / root` frames is, and every record that used the sound has that property. The bass is the one voice that is *not* sampled, for the reason it was not then. |
| [`boombox.gen`](boombox.gen) | **Electro, and a vocoder that needs two channels.** A string machine holds the chord and is the carrier; a ts1 line in sixteenths is the modulator and is never heard at all -- `fx/vocoder.dsp` sits on its channel and replaces everything it plays with sixteen bands of the carrier. `side = pads` is what makes that possible and is why the piece is here: an effect used to hear its own channel's sum and nothing else, so a vocoder could not be written. The modulator's `amp` is a drive rather than a volume, and both factors are amplitudes, so the staging is quadratic. |
| [`outrun.gen`](outrun.gen) | **Synthwave, and why an arpeggiator is a `gen::` stage.** An `xform::` stage runs when an event passes through it and a `gen::` stage is woken by the transport, and an arpeggiator needs both -- it hears a chord once a bar and then has to put sixteen steps between that event and the next -- so `xform::arp` holds a chord it is never asked about and the channel is silent. The flanger is on the channel and not in the voice, through zero, so the sweep is on the figure rather than restarted by every note in it; the `Jet` knob is signed feedback, and the two signs are two different records. |
| [`riviera.gen`](riviera.gen) | **Italo disco.** Root and octave in eighths with `xform::accent` marking one step a beat, and `bass.dsp` turning a velocity over its threshold into filter envelope and resonance rather than level -- the 303's accent circuit, which is what makes the part sound played. The same walk on three chains again: a Solina whose ensemble chorus lives *inside* the voice because that is what the instrument is, and a clav on `.r.f.r.t`, eighths with the downbeats left out. The orchestra hit is a recording of this tree's own `stab` and `brass` at unison, one per eight bars from `euclid`'s `fill` pool -- which moves only on the cycles that fire, where a pool consumed every bar would land on the same pitch every time. |

## Voicing

A chord is which notes are in it; a voicing is which octave each of them is
sung in. Until now the second answered to the first: `xform::harmonize`
stacked the degrees on the root, wherever the root happened to be, so a
progression moved in parallel blocks and the ear heard a row of chords rather
than a few voices going somewhere. `lead = 1` keeps the notes and chooses the
octaves -- each voice to the register nearest the voice it replaces in the
chord before, inside `span` semitones of the root -- which is how a root
that leaps a fifth can leave two of its three voices standing still.

`village.gen`, `orrery.gen` and `anthem.gen` ask for it. The pieces that
harmonize a melody rather than a progression -- `colony.gen`, `belfry.gen` --
do not, because there the chord is meant to follow the line.

## A note that moves

Three nodes for the things a held note does that an envelope cannot.
`misc::vibrato` bends a frequency by a number of *cents*, so the bend is the
same interval at every pitch, and waits out a `delay` before it starts --
which is what makes it a player's vibrato rather than an LFO, since a note
shorter than the delay comes out exactly as it did before. `delay::allpass` is
Schroeder's: a delay line fed back and fed forward at once, flat at every
frequency, which is what `fx/hall.dsp` needed to stop being a bank of combs
with countable echoes. `delay::chorus` reads a short line at two or three
places an LFO keeps moving, so the copies drift in and out of tune with the
original -- a section rather than a player.

An `effect` statement at the top of a piece puts a graph on the **mix**: after
every channel has been summed, before the master gain and the output limiter.
It is the same clause an instrument carries and the same object the engine
runs, in the one place a reverb belongs and the only place a limiter can be --
what a limiter limits is the sum, and no channel can see it. `fx/limiter.dsp`
is a peak follower into a gain, and `warehouse.gen` plays through one.

`ladder.dsp` and `brass.dsp` carry a vibrato by default and `supersaw.dsp`
carries the control at zero; `anthem.gen`'s lead turns it up, and
`invention.gen`'s Moog plays through `fx/chorus.dsp` on its channel, which is
the box that turned one synthesizer into a section on the record it is in the
spirit of.

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
and `counterpoint` (village), `steps` and `pump`
(warehouse, anthem), `transpose` (anthem), `accent` (warehouse, anthem),
`vary` (warehouse, anthem), and `euclid`'s `fill` pool (warehouse,
anthem, riviera). pearl and riviera add `progression` walked by three
chains on one seed (both), `bassline` as an octave figure and as an
offbeat comp (riviera), `accent` into an instrument's accent circuit
(riviera) and `swing` on a sixteenth grid (pearl).
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
(weather, tide), `meter` and `section` -- the arrangement, with `section end`
closing it (warehouse, anthem, pearl, riviera), two channels of one graph so
that each can carry its own effect (pearl), pinned and unpinned seeds (all of
them, both ways).

See [`../docs/GEN_FORMAT.md`](../docs/GEN_FORMAT.md) for the language and
[`../docs/UNIFICATION.md`](../docs/UNIFICATION.md) for where it is going.
