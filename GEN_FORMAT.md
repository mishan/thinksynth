# The `.gen` format

A `.gen` file describes a generative piece: which composer plugins run, how
they are chained, what they are allowed to play, and which knobs the piece
exposes. It is to composers what `.patch` is to DSPs — it names plugins and
sets their params — but shaped like the DSP language, because a piece is a
small graph, not a flat list.

The lexical layer is the `.dsp` one — not a copy of it but literally the same
scanner (`libthink/thLexer.h`): `#` comments, `;` statement ends, `=`, `::`,
`{ }` blocks, quoted strings, numeric literals with optional units. Units are
ordinary words to the lexer rather than keywords, so `s` and `beats` cost it
nothing; what a unit *means* is the loader's question, which is how `.gen`
keeps seconds where `.dsp` folds milliseconds into samples.

Two marks read differently here than in a `.dsp`, and the loader puts them
back together after the shared scan: a leading `-` belongs to the number after
it (there is no arithmetic in this language for it to be an operator in), and
`@name` is one token (there is no `@` operator either). Both require the mark
to sit directly against what follows, so `- 5` is an error, not minus five.

## 1. The language

```
name "Airports";
author "Misha Nasledov";
description "In the spirit of Music for Airports 2/1.";

tempo 60;                       # optional; only clocked stages need it
seed 1978;                      # optional; present means replayable

@density = 0.85;                # a piece knob -- same syntax, same
@density.widget = 1;            # metadata, same panel as a .dsp chanarg
@density.min = 0;
@density.max = 1;
@density.label = "Density";

scale fmin "F3 Ab3 C4 Db4 Eb4 F4 Ab4";

instrument pad {                # the piece carries what it is played on
    dsp  "amb01.dsp";
    a    = 900 ms;
    fmin = 0.06;
};

chain loop1 {
    stage src gen::eno_line {   # <name> <category>::<plugin>
        notes  = "Ab3";
        period = 19.4 s;
        jitter = 1.5 s;
        prob   = @density;      # live binding, not a copy
        hold   = 6 s;
        vel    = 70;
    };
    stage q xform::quantize {
        scale = fmin;
    };
    sink { instrument = pad; };
};
```

## 2. Time carries units, and the unit decides the clock

A duration param is a number with a unit: `s`, `ms`, or `beats` (alias `b`).

```
period = 19.4 s;                # free-running: seconds are seconds
period = 4 beats;               # clocked: converted via tempo at fire time
```

This is the whole clocked/free distinction — it lives in the *value*, not in
the plugin. The same `eno_line` is a tape loop with `period = 21.3 s` and a
step in a pulse with `period = 1 beats`. Because the scheduler integrates
beats rather than deriving them, a beat-valued duration keeps meaning
something across tempo changes mid-piece.

A bare number on a duration param is an error, not a defaulted second. Units
were optional in `.dsp` and the corpus shows what that buys: every reader of
an old file guessing. Not this time.

## 3. Piece knobs are chanargs

`@density` above is stored, edited and displayed by exactly the machinery
that handles a `.dsp` chanarg — `.widget`, `.min`, `.max`, `.label`,
`.units`, `.group` all mean what they already mean. `name`, `author` and
`description` are stored the same way the DSP parser stores them.

Binding a stage param to a knob (`prob = @density;`) is the composer-world
`ARG_CHAN`: the param store resolves it live, so dragging the knob changes
every stage bound to it, mid-piece, with no plumbing in the plugin. The
plugin just calls `params->get()` as always.

A param not bound to a knob is a plain value (`ARG_VALUE`). There is no
composer equivalent of `ARG_NODE` in v2 — stages do not wire params to each
other. What flows between stages is events, and only events. If wiring turns
out to be wanted, it is an extension, not a reinterpretation.

The same `@density` may also drive an instrument's chanarg — see §4b. One
declaration, one slider, both sides of the boundary.

## 4. Scales are named objects

```
scale fmin "F3 Ab3 C4 Db4 Eb4 F4 Ab4";
```

A `scale` statement parses its note names once, at load, with one shared
parser — no plugin ever parses pitch text again (`THC_PARAM_NOTESET` receives
the resolved list). A NOTESET param accepts either a scale identifier or a
quoted literal list; the literal is for one-off pools like a single-note tape
loop, the identifier is for the pool three transformers share. Note names are
`[A-G]`, optional `#`/`b`, octave; middle C is `C4`.

This resolves the question the plugin API left open: pitch pools are not
strings threaded through param tables — they are declared once and referenced
by name, and the string form of the param exists only at the file boundary.

## 4a. Presets are named chanarg vectors

```
preset dusk {
    res  = 0.86;
    fmin = 0.04;
    fmax = 0.30;
};
```

A patch's declared chanargs are a vector of floats, and a vector of floats is
something a composer can interpolate between, breed from, or save. Giving one
a name is what lets a piece refer to a *timbre* the way it already refers to a
scale. `THC_PARAM_PRESET` receives the resolved vector — `"res=0.86,fmin=0.04"`
— exactly as `THC_PARAM_NOTESET` receives resolved pitches, so no plugin ever
looks a preset up.

The values are plain numbers. A knob inside a preset would make it not a
vector but an expression that happens to have a value right now, and both
things a preset exists for want the fixed reading; the knob belongs on the
stage that *uses* the preset, where it already works. Declaration order is
kept, because the vector is the point. A preset must be declared before it is
referenced, like a scale, and a preset that sets nothing is an error rather
than a silent no-op.

Unlike a note set, there is no quoted-literal form. A one-off pitch pool is a
reasonable thing to write inline; a one-off timbre vector spelled as text is a
preset that cannot be morphed towards, bred from or saved under a name, which
is the whole reason the noun exists.

This is the limit of a composer's reach into an instrument: the args the patch
chose to declare, and no deeper. See `COMPOSITION_HANDOFF.md` §9.

## 4b. Instruments are what the piece is played on

```
instrument pad {
    dsp  "amb01.dsp";           # the graph, by name
    a    = 900 ms;              # the values that make it this instrument
    fmin = 0.06;
};
```

A `.gen` used to name MIDI channels in its sinks and leave the question of
what was loaded on them to whoever opened the file — which is why every piece
in `gen/` carried a paragraph at the top telling you what to go and load
first, and why "open it and press play" was true of none of them. An
`instrument` block answers that inside the file.

The shape is a `.patch` said out loud: a graph, then the chanarg values that
make it this instrument rather than that graph's defaults. Two things it can
do that a `.patch` cannot.

**The values carry units.** A `.patch` stores every chanarg already folded, so
an envelope in it reads `a 39690` — a sample count at one particular rate. Here
it is `a = 900 ms`, and the fold happens on the way in, against the unit the
chanarg was *declared* with and at the rate the synth is actually running. The
unit has to match that declaration: `ms` on an arg written in milliseconds, `%`
on one written as a percentage, and a bare number on everything else. A unit
where none belongs is refused, and so is a bare number where one does — the
same rule §2 applies to a stage's durations, for the same reason.

**It has a name, and a sink can bind to the name.** That is the point. Routing
stops being a number the author and the listener have to agree about out of
band.

`dsp` is required, and is a keyword inside the block rather than a chanarg that
happens to take a string. An instrument must be declared before it is
referenced, like a scale or a preset.

Writing the graph out inline instead of naming it is the other half of the
same idea and is not here. By reference alone delivers the self-contained
file, which is what that step was for.

**A value may be a knob.**

```
instrument pad {
    dsp  "amb01.dsp";
    fmin = @warmth;             # one knob, both worlds
    r    = @tail ms;            # the unit applies to the knob's numbers
};
```

`@warmth` is the same `@warmth` a stage param binds to — one declaration, one
slider, one entry in the panel, reaching a composer and an instrument at once.
That is the whole of §3's binding namespace applied on both sides of the
boundary rather than only on one.

The direction differs, and it is worth knowing why. A stage param bound to a
knob is *read* through it: a composer asks its param store for a value whenever
it wants one. A chanarg cannot work that way, because the thing that reads a
chanarg is the audio graph and the only value it will ever see is the one
sitting in its `thArg`. So an instrument binding is a **push**: the knob moves,
the chanarg is set. The knob's current value is pushed at load too, so a piece
sounds like its file the moment it opens rather than one knob-move later.

The unit rule is the literal's rule, unchanged: `r = @tail ms` because `r` is
folded from milliseconds, `fmin = @warmth` because `fmin` is not folded at all,
and getting either backwards is refused. A knob's number is exactly as unitless
as a number in the file — an envelope on a bare binding would be a slider whose
top end is forty milliseconds — so the unit is stated at the value site and
applied on every move.

**A knob and a named chanarg sink may not share an arg.** Both are pushes, so
both writing `fmin` is last-writer-wins: the walk wins every time it fires and
the slider appears dead a second after you let go of it. There is no reading of
the file where that was the intention and it is invisible from either end, so
the loader refuses it and says which knob. If what was wanted is a starting
point the walk moves away from, that is a plain number.

`chanarg = "*"` is outside that check, for exactly the reason it is outside the
"does this arg exist" one: the targets are in the events, and a composer
emitting a vector may or may not ever name a knob-bound arg. A `*` sink and a
knob binding on the same instrument is the one way left to write the fight, and
nothing can catch it for you.

**Chanargs remain the whole of a composer's reach into an instrument.** A knob
binding widens *who* may drive a declared arg, not *what* may be driven. The
sentence in §4a still holds: an instrument's surface is what it declares.

**Channels are allocated, not declared.** Each instrument gets the lowest
channel that no `channel = N` sink in the file has claimed and that nothing
else is already loaded on, taken in declaration order — so the first instrument
usually lands on channel 1, where somebody looking for it would look. Opening a
piece never replaces a patch you loaded yourself; it takes what is free. Given
the same starting state the assignment is the same every time, which matters
more than it sounds, because patch tabs, saved mixer settings and the roll's
per-channel colors all key off it.

Two composers take presets today, and they are the two halves of tier 2.
`gen::morph` travels the line between two of them. `gen::breed` does not know
where it is going: it keeps a population of chanarg vectors and breeds them,
and the corridor it may search is the interval the named presets span, widened
by its `spread` param. A component neither preset mentions cannot be invented,
and a component only one of them names has nowhere to travel — so the sentence
above is arithmetic in that plugin rather than a rule someone has to remember.

## 5. Chains

A `chain` is a named, *ordered* pipeline. Order in the file is order of
execution — which is why the keyword is `stage` and not `node`: in a `.dsp`,
statement order is irrelevant and edges carry the topology; in a chain, order
IS the topology, and using the same word for both would invite the wrong
intuition in whoever edits the file.

A chain body holds, in order:

- optionally `input midi;` — the chain is fed by live MIDI arriving on the
  sink channel (arpeggiators, Markov training). A chain may have an input, a
  generator stage, both, or neither only if it is all transformers reached by
  `input midi`.
- zero or more `stage` blocks. A stage whose plugin exports `tick` is a
  generator; one exporting `receive` is a transformer; the loader checks that
  what the file asks of a plugin matches what it exports and rejects the
  file otherwise, by name and line.
- one or more `sink` blocks, always last:

```
sink { instrument = pad; };                     # notes -> the piece's own pad
sink { instrument = pad; chanarg = "fmin"; };   # values -> that pad's knob
sink { channel = 4; };                          # notes -> MIDI channel 4
sink { channel = 3; chanarg = "cutoff"; };      # values -> a patch knob
sink { channel = 3; chanarg = "*"; };           # values -> the knob each
                                                #   event names for itself
```

A module may export both, and several do. `gen::markov` trains on what it
hears and emits its own walk; `gen::life` plays Conway's board and lets an
upstream stage *draw* on it, turning arriving pitches into cells. A stage
like that is a generator in first position and a transformer anywhere else,
which is what makes a chain a pipeline rather than a list — `colony.gen`
runs a Euclidean rhythm into a Life board into a markov into a harmonizer,
and each stage hears only the one before it. What a stage does with what it
hears is its own business, and it does not have to be "pass a modified
copy along": a stage whose `pass` is 0 consumes its input entirely, so
everything downstream is a consequence of the input rather than a version
of it.

A sink names **an instrument or a channel, never both**; a sink that names
both is refused rather than have the loader pick one. `instrument = pad` is
the primary spelling and the one a self-contained piece uses. `channel = N`
stays in the language for the case it was always really for: driving a patch
this piece does not own and did not load.

**Channels are 1–16.** That is the number on the main window's patch tab and
in the Keyboard window's spinner, and it is what every sequencer shows; the
wire and the engine count from zero, and the conversion happens here at the
file boundary the way note names are resolved here rather than in a plugin.
`channel = 0` is an error rather than channel 1, and says why — it is the one
spelling that can tell a file written for the old 0–15 numbering apart from
one written for this, and a piece silently playing a channel out is worse than
a piece that refuses to load.

### 5a. A stage can be a DSP node

```
chain breathing {
    stage lfo  osc::simple { freq = 0.05; waveform = 0; };
    stage half math::mul   { in0 = lfo->out;  in1 = 0.45; };
    stage mid  math::add   { in0 = half->out; in1 = 0.5; };

    stage src gen::eno_line { prob = mid->out; ... };
    sink { instrument = pad; };
};
```

A category that is not `gen` or `xform` names a **plugin family from the other
world** — the same `.so` files a `.dsp` is built out of, spelled the way a
`.dsp` spells them, wired to each other with the same `->`. They run on the
composer's side of the program at fifty windows a second, one sample at a time,
which is what a control signal is; `freq = 0.05` is a twenty-second cycle here
and a very low note in a patch, and the plugin cannot tell the difference
because it divides by whatever rate it is being run at.

**A node is not a stage, though both are spelled `stage`.** Events do not flow
through it: nothing is handed to it and nothing comes out the far side. It
holds a value, and the composer stages read that value with `->` at the moment
they want it — so a chain full of nodes still needs a generator or `input midi`
in it, and a node never satisfies that. The keyword stays `stage` because
inside a chain everything is one; what says which world a module comes from is
the category, exactly as it always was.

**Nothing scales the signal for you.** An oscillator runs −1 to +1 and a `prob`
wants 0 to 1; `math::mul` and `math::add` are what a patch would use and they
are right there. Three lines instead of one, in exchange for no hidden mapping
and no second meaning for the arrow depending on which side it lands on.

A node arg takes a number, another node's output, or a piece knob
(`in1 = @depth;`) — the same knob a stage param binds and an instrument chanarg
reads, one world further out.

**Families that mean nothing at control rate are refused, by name.** `osc`,
`env`, `math`, `logic`, `filt` and `misc` are shapes over time, and time at
fifty a second is still time. `delay` and `fft` count in *samples*, and a sample
here is a fiftieth of a second rather than twenty microseconds — a 4410-sample
delay is a hundred milliseconds on the audio thread and a minute and a half on
this one. They would run, and produce numbers, and the numbers would mean
something no author intended, which is worse than refusing because it looks
like it worked. `osc::static` is refused for a different reason: it draws from
the global random generator, and a piece using it would not replay.

Nodes step on **transport time**, so a pause freezes them where they are and a
rewind starts them again from the top. How far an LFO has travelled is a
function of where the transport got to, not of how many frames went by — which
is what lets a piece with nodes in it pass the same replay gate every other
piece passes.

### 5b. A composer can reshape the instrument

Two event kinds go the other way from a note: instead of asking an instrument
to play something, they change what the instrument *is*.

```
instrument voice { dsp "amb01.dsp"; ... };
instrument bell  { dsp "amb01.dsp"; a = 4 ms; ... };

chain swapping {
    stage m gen::swap { instruments = "voice,bell"; every = 40 s; };
    sink { instrument = voice; };        # the slot it rebuilds
};

chain sensitivity {
    stage r gen::reshape { node = "fmap"; arg = "inmax";
                           from = 1; to = 0.25; every = 11 s; };
    sink { instrument = voice; };
};
```

**A swap** rebuilds the sink's channel around a different instrument. It goes
through the same patch-load path a person clicking in the Patch Selector uses —
which means it **cuts whatever is sounding**: the channel is replaced at a
window boundary and the old graph's voices stop there, with no release. (What
that path has always promised is that the outgoing channel is not freed under
the audio thread. That is a promise about lifetimes, and this section claimed
the other one for a while by confusing the two.) Write swaps on a clock
measured in tens of seconds, or onto a channel that is resting; `THC_EV_NODEARG`
is the edit that leaves sounding voices alone. `instruments` is resolved at the
file boundary exactly as a scale and a preset are: a bare name for one, a quoted
comma-separated list for several, and every name checked before the piece
loads.

A swap may only land on a channel the piece **declares an instrument for**.
Rebuilding a graph is not like writing a chanarg, where the worst case is a
number: it throws away whatever was on the channel, and a rewind could not put
it back, because a channel no declaration names is a channel nothing restores
from. A `sink { channel = 5; }` can still carry a swap chain — the swap is
simply refused, by name, in the log. And a swap to the instrument already there
does nothing at all rather than rebuilding a graph into a copy of itself: a
`gen::swap` has a list and a clock and cannot see what its sink is playing, so
a list beginning with the sink's own instrument would otherwise cut every
sounding voice on the opening tick.

**A node-arg edit** changes one constant *inside* the graph — a node in the
`.dsp` and one of its args, which is emphatically not a chanarg. §4a says the
args a patch declares are the whole of a composer's reach, and that stays true
of chanargs; this is the different mechanism `COMPOSITION_HANDOFF.md` §9
promised rather than a widening of that one. The consent moved rather than
vanished: a piece reaching this deep has said so in a line anyone can read.

Only an arg that is **already a constant** may be set. Anything wired is
refused — to another node's output, to a `@chanarg`, to a note property —
because writing a number over a wire would silently unwire the graph, which is
an add/remove/rewire edit wearing a value edit's clothes. The chanarg case is
the one that bites hardest and is easiest to miss: `outmin = @fmin` looks like a
number in the file, and a number written over it would kill that channel's
`fmin` for the rest of the session with the slider still on screen. A module's
`ARG_STATE` scratch is refused too, and so is an arg the module never declared.

**Both are events**, and that is the whole rate limit: scheduled, so they
happen on the transport; sparse, so nothing can thrash a channel; replayed from
the seed, so a piece sounds the same twice; and drawn on the roll, so you can
watch one coming. A rewind puts every instrument back as the file declares it,
because after a swap the channels no longer say what the file says.

A structure edit reaches **every** sink of its chain. The note/chanarg filter is
a rule about notes and chanargs; an edit is neither, and both kinds of sink name
the channel it needs — so fan-out means what fan-out means everywhere else.

Two sinks is fan-out: every event leaving the last stage is delivered to
each. A `chanarg` sink delivers `THC_EV_CHANARG` events and silently drops
notes; a plain sink does the reverse. That rule is in the sink, not the
stage, so one generator can drive a melody and a filter sweep at once.

A named `chanarg` sink *overwrites* the name on every event passing through
it, which is right for a walk or an envelope: the plugin produces a number
and has no business knowing which knob it lands on. `chanarg = "*"` is for
the case that breaks — a composer producing a whole vector, several knobs at
once, each event already knowing which one it is. One sink per knob cannot
say that, because every sink would deliver the same value. `*` cannot collide
with a real name, since a chanarg is a `.dsp` identifier; anything else that
is not one is refused at load rather than failing silently at delivery.

## 6. Grammar

```
genfile     : statement*
statement   : infostring | tempo | seed | knob | knobmeta | scale
            | preset | instrument | chain
infostring  : ("name" | "author" | "description") STRING ";"
tempo       : "tempo" NUMBER ";"
seed        : "seed" NUMBER ";"
knob        : CHANARG "=" NUMBER ";"
knobmeta    : CHANARG "." WORD "=" (NUMBER | STRING) ";"
scale       : "scale" WORD STRING ";"
preset      : "preset" WORD "{" presetval* "}" ";"
presetval   : WORD "=" NUMBER ";"
instrument  : "instrument" WORD "{" instrstmt* "}" ";"  # exactly one dsp
instrstmt   : "dsp" STRING ";"
            | WORD "=" (NUMBER | CHANARG) argunit? ";"  # CHANARG = a knob
argunit     : "ms" | "%"                               # what .dsp folds
chain       : "chain" WORD "{" input? stage* sink+ "}" ";"
input       : "input" "midi" ";"
stage       : "stage" WORD WORD "::" WORD "{" param* "}" ";"
                                                       # gen/xform: a
                                                       #   composer
                                                       # anything else: a
                                                       #   dsp node (5a)
param       : WORD "=" value ";"
value       : NUMBER unit? | CHANARG | STRING | WORD    # WORD = scale or
                                                       #   preset ref
            | WORD "->" WORD                           # a node's output
nodearg     : WORD "=" (NUMBER | CHANARG | WORD "->" WORD) ";"
unit        : "s" | "ms" | "beats" | "b"
sink        : "sink" "{" sinkparam* "}" ";"
sinkparam   : ("instrument" "=" WORD | "channel" "=" NUMBER
              | "chanarg" "=" STRING) ";"
                                                       # instrument or
                                                       #   channel, not both
                                                       # channel is 1-16
                                                       # STRING = a name
                                                       #   or "*"
```

`CHANARG`, `STRING`, `NUMBER`, `WORD` and the punctuation are the existing
`.dsp` tokens. `ms` is already a token; `s` and `beats`/`b` join it. `->` is a
`.dsp` token too and means in a `.gen` exactly what it means in a `.dsp`:
reading a node's output. `%` reaches `.gen` for one purpose only — the unit suffix an
instrument value needs when the chanarg it lands on was declared as a
percentage. Everywhere else in a `.gen` it is a stray character, the way `+`
is.

## 7. Rules for anything that writes these files

The GUI writes `.gen` files by *editing the text* (`src/thcGenEdit.cpp`),
not by regenerating it from a model — the same decision NodeEdit made for
`.dsp`, so an author's comments and blank lines survive any sequence of
GUI edits. Each operation replaces exactly the token span it is aimed at,
located through the loader's own lexer. The rules below bind what gets
written *into* those spans, and what a freshly generated block (a new
stage, a new chain) contains:

- Write stages in execution order; there is no other order to recover.
- Write durations back in the unit the author used. A user who wrote
  `4 beats` and reads back `4.000000 s` at tempo 60 has been lied to, even
  though the piece sounds identical.
- Write every param the plugin registers, including ones still at their
  defaults. A `.gen` should survive a plugin's defaults changing — this is
  the lesson of `noargs/`.
- Knob bindings round-trip as `@name`, never as the knob's current value.
- A preset reference round-trips as the preset's bare name. There is no
  literal form to fall back on, so a writer that could not name it would have
  nothing to write.
- A preset's components stay in the order its author wrote them, and a new one
  is appended rather than filed into a canonical slot. The vector is the point,
  and reshuffling someone's file into the order this writer prefers is an edit
  nobody asked for — the same rule the `.dsp` writer follows for `@x.min`.
- A preset that sets nothing does not load, so no editor operation may leave
  one: removing the last component is refused, and a new preset arrives with at
  least one. Removing a preset something still names is refused too, and says
  which stage — unlike a scale, there is no literal to inline in its place.
- An instrument value round-trips in the unit its author wrote, exactly as a
  duration does, and for the same reason: `900 ms` read back as `39690` is a
  file that has been lied to about what it says.
- A sink round-trips in the spelling it was written in. Switching between
  `instrument = pad` and `channel = 4` replaces that one statement — there is
  no sense in which one can be edited into the other, and a sink left carrying
  both would not load.
- `seed` is written if and only if the user pinned it. A generated file with
  a seed the user never chose silently freezes a piece that was meant to
  breathe.

## 8. Where each piece lands

| in the file            | in the engine                                       |
| ---------------------- | --------------------------------------------------- |
| `stage` block          | `thcStage`: plugin instance + `thcParamStore`       |
| param `= number`       | store value (the composer-world `ARG_VALUE`)        |
| param `= @knob`        | live binding (the composer-world `ARG_CHAN`)        |
| `= n beats`            | converted via transport tempo when the value is read|
| `scale`                | resolved note list, shared by reference             |
| `preset`               | resolved chanarg vector, shared by reference        |
| `instrument` block     | `thcInstrument`: a graph loaded onto an allocated channel |
| `sink { instrument = }`| that instrument's channel, filled in after the parse |
| instrument `= @knob`   | a push: the knob's changed signal sets the chanarg   |
| `chanarg = "*"`        | a sink that keeps the name each event carries       |
| `sink`                 | delivery target(s) in `thcScheduler::deliver`       |
| a `dsp` family stage   | a node in the chain's `thcNodeHost`, at control rate |
| `gen::swap`            | `THC_EV_PATCH`: the sink's channel is rebuilt        |
| `gen::reshape`         | `THC_EV_NODEARG`: a constant in that channel's graph |
| param `= node->arg`    | the composer-world `ARG_NODE`: the node's live output |
| `input midi`           | `thcScheduler::injectMidi` routing entry            |
| `tempo`, `seed`        | transport init; master seed for `reset()` replays   |
| `@knobs` + metadata    | the existing chanarg/param-panel machinery          |
