# The `.gen` format

A `.gen` file describes a generative piece: which composer plugins run, how
they are chained, what they are allowed to play, and which knobs the piece
exposes. It is to composers what `.patch` is to DSPs — it names plugins and
sets their params — but shaped like the DSP language, because a piece is a
small graph, not a flat list.

The lexical layer is the `.dsp` one, unchanged: `#` comments, `;` statement
ends, `=`, `::`, `{ }` blocks, quoted strings, numeric literals with optional
units. The lexer needs two new unit tokens and nothing else.

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
    sink { channel = 3; };
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
sink { channel = 3; };                          # notes -> MIDI channel 3
sink { channel = 2; chanarg = "cutoff"; };      # values -> a patch knob
```

Two sinks is fan-out: every event leaving the last stage is delivered to
each. A `chanarg` sink delivers `THC_EV_CHANARG` events and silently drops
notes; a plain sink does the reverse. That rule is in the sink, not the
stage, so one generator can drive a melody and a filter sweep at once.

## 6. Grammar

```
genfile     : statement*
statement   : infostring | tempo | seed | knob | knobmeta | scale | chain
infostring  : ("name" | "author" | "description") STRING ";"
tempo       : "tempo" NUMBER ";"
seed        : "seed" NUMBER ";"
knob        : CHANARG "=" NUMBER ";"
knobmeta    : CHANARG "." WORD "=" (NUMBER | STRING) ";"
scale       : "scale" WORD STRING ";"
chain       : "chain" WORD "{" input? stage* sink+ "}" ";"
input       : "input" "midi" ";"
stage       : "stage" WORD WORD "::" WORD "{" param* "}" ";"
param       : WORD "=" value ";"
value       : NUMBER unit? | CHANARG | STRING | WORD    # WORD = scale ref
unit        : "s" | "ms" | "beats" | "b"
sink        : "sink" "{" sinkparam* "}" ";"
sinkparam   : ("channel" "=" NUMBER | "chanarg" "=" STRING) ";"
```

`CHANARG`, `STRING`, `NUMBER`, `WORD` and the punctuation are the existing
`.dsp` tokens. `ms` is already a token; `s` and `beats`/`b` join it.

## 7. Rules for anything that writes these files

The GUI will write `.gen` files, so the writing rules exist from day one
rather than being archaeology later:

- Write stages in execution order; there is no other order to recover.
- Write durations back in the unit the author used. A user who wrote
  `4 beats` and reads back `4.000000 s` at tempo 60 has been lied to, even
  though the piece sounds identical.
- Write every param the plugin registers, including ones still at their
  defaults. A `.gen` should survive a plugin's defaults changing — this is
  the lesson of `noargs/`.
- Knob bindings round-trip as `@name`, never as the knob's current value.
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
| `sink`                 | delivery target(s) in `thcScheduler::deliver`       |
| `input midi`           | `thcScheduler::injectMidi` routing entry            |
| `tempo`, `seed`        | transport init; master seed for `reset()` replays   |
| `@knobs` + metadata    | the existing chanarg/param-panel machinery          |
