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
    sink { channel = 4; };
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
sink { channel = 4; };                          # notes -> MIDI channel 4
sink { channel = 3; chanarg = "cutoff"; };      # values -> a patch knob
sink { channel = 3; chanarg = "*"; };           # values -> the knob each
                                                #   event names for itself
```

**Channels are 1–16.** That is the number on the main window's patch tab and
in the Keyboard window's spinner, and it is what every sequencer shows; the
wire and the engine count from zero, and the conversion happens here at the
file boundary the way note names are resolved here rather than in a plugin.
`channel = 0` is an error rather than channel 1, and says why — it is the one
spelling that can tell a file written for the old 0–15 numbering apart from
one written for this, and a piece silently playing a channel out is worse than
a piece that refuses to load.

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
            | preset | chain
infostring  : ("name" | "author" | "description") STRING ";"
tempo       : "tempo" NUMBER ";"
seed        : "seed" NUMBER ";"
knob        : CHANARG "=" NUMBER ";"
knobmeta    : CHANARG "." WORD "=" (NUMBER | STRING) ";"
scale       : "scale" WORD STRING ";"
preset      : "preset" WORD "{" presetval* "}" ";"
presetval   : WORD "=" NUMBER ";"
chain       : "chain" WORD "{" input? stage* sink+ "}" ";"
input       : "input" "midi" ";"
stage       : "stage" WORD WORD "::" WORD "{" param* "}" ";"
param       : WORD "=" value ";"
value       : NUMBER unit? | CHANARG | STRING | WORD    # WORD = scale or
                                                       #   preset ref
unit        : "s" | "ms" | "beats" | "b"
sink        : "sink" "{" sinkparam* "}" ";"
sinkparam   : ("channel" "=" NUMBER | "chanarg" "=" STRING) ";"
                                                       # channel is 1-16
                                                       # STRING = a name
                                                       #   or "*"
```

`CHANARG`, `STRING`, `NUMBER`, `WORD` and the punctuation are the existing
`.dsp` tokens. `ms` is already a token; `s` and `beats`/`b` join it.

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
| `chanarg = "*"`        | a sink that keeps the name each event carries       |
| `sink`                 | delivery target(s) in `thcScheduler::deliver`       |
| `input midi`           | `thcScheduler::injectMidi` routing entry            |
| `tempo`, `seed`        | transport init; master seed for `reset()` replays   |
| `@knobs` + metadata    | the existing chanarg/param-panel machinery          |
