# The `.dsp` and `.patch` formats

The DSP language is parsed by bison/flex (`libthink/thinklang.yy`,
`thinklex.ll`). It is a node-graph description language, which is why the node
editor maps onto it with essentially no impedance mismatch.

The lexer is shared with the `.gen` format (`libthink/thLexer.h`): one scanner
hands both languages tokens carrying line numbers and byte spans, and each
grammar decides which words it has opinions about. So what this document says
about comments, strings, numbers and punctuation is true of a `.gen` file too,
and cannot quietly stop being true of one.

Most of this document exists because the node editor has to *write* these files
as well as read them, and writing found constraints that reading never revealed.
Anything that edits a `.dsp` needs the rules in section 3.

## 1. The DSP language

```
name "TS-1";
author "Leif Ames";
category "Synths";          # optional; where a chooser files it

@cutoff = 4;              # a channel arg = user-facing knob
@cutoff.widget = 1;       # .min .max .label .widget .units .group
@cutoff.min = 0;          # .step .values
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

### One arg is a name and not a number

```
node kick osc::sample {
    file = "kick808.wav";       # a quoted name, not a value
    freq = freq->out;
    root = freq->out;
};
```

`osc::sample` has to be told which file to play, and a filename is not a
number. The right-hand side of a node arg may therefore be a quoted string,
which makes that arg an `ARG_TEXT`: it holds the name the file wrote, and it
holds a single zero where the numbers would be, so anything that reads it
without asking what kind of arg it is — an expression that names it, a probe
armed on it, a panel drawing it — reads a 0 rather than whatever was on the
heap. Only the plugin that declared it looks at the name.

**It is not a control, and cannot become one.** A number is a thing a slider
moves, a MIDI controller reaches, a preset stores and `gen::walk` sweeps; a
filename is none of those. So there is no `@file` and no `file = @kit` — the
grammar takes a literal string there and nothing else, and the node editor
shows the name rather than offering a box to type a number into. A kit is
therefore one `osc::sample` node per drum, each gated by the note it answers
to, rather than one node with its filename swept.

For the same reason a string cannot appear inside arithmetic, be wired from
another node's output, or carry a unit. The one other place the grammar has
always read a quoted string — `@x.label = "Cutoff"` — is metadata about a
control and not a value either.

### Arithmetic over signals

An arg's right-hand side may be an expression, and its leaves may be signals:

```
node osc2 osc::simple {
    freq = freq->out * exp2(@cents / 1200);   # a detune in cents
};
node ionode {
    out0 = (osc1->out + osc2->out) * 0.5;     # the average of two saws
};
```

This is **sugar, and the audio path never sees it.** `thSynthTree::desugarExprs`
rewrites each expression into the `math::` nodes it stands for during
`finishParse`, before `buildArgMap` indexes anything, so probes, layout,
`dspcheck`, the wasm build and `thcNodeHost` all see an ordinary graph. There is
no separate vector case and nothing new to define: the language already says a
constant is a buffer of constants, so scalar and vector are the same expression
evaluated per sample.

The nodes are named after the arg they feed — `osc2.freq#1`, `osc2.freq#2`,
innermost first — so a log or a probe that names one says where it came from.
`#` starts a comment in this grammar and therefore cannot appear in an authored
node name, which is what makes the collision unconstructible rather than
unlikely.

- **An all-constant expression folds at parse, as it always has.** `a = 5 * 2`
  is one number and no node, which is why this change leaves every shipped file
  rendering bit for bit as it did.
- **A single leaf is not an expression.** `in = osc->out` is the `ARG_POINTER`
  it has always been and `in = @cut` the `ARG_CHANNEL`; only something with an
  operator or a call in it becomes nodes.
- **The operators are `+ - * /`.** `%` still means modulo between two numbers
  and a percentage after one, and is refused over a signal: there is no node
  for it, and desugaring to something that is nearly a modulo is worse than
  saying so. `7 % 0` is refused too — an integer division by zero is a signal,
  not a number, and it used to take the process down with it.
- **`- x` is `x * -1`**, which is the node that already exists, and it binds to
  the operand rather than to the rest of the line: `-1 + 2` is `1`. It used to
  sit at the top of an expression and scope over everything to its right, which
  made `-1 + 2` come out `-3`, made `a->out * -0.5` a syntax error, and made
  the same text mean two things depending on whether it was in a `.dsp` or a
  `.gen`. Nothing in the corpus wrote one, which is what let it move.
- **The functions are `pow(a, b)`, `exp2(x)`, `abs(x)`, `min(a, b)`,
  `max(a, b)` and `clamp(x, lo, hi)`**, each a `math::` plugin a file may also
  write by hand. Functions rather than a `^` operator, so the language gains no
  precedence anyone has to remember.
- **`*` and `/` bind tighter than `+` and `-`**, and all four are
  right-associative: `a - b - c` is `a - (b - c)`, and a `-` takes the
  additions after it too, so `1 - 2 + 3` is `-4`. That is what the constant
  folding has done since the language existed. `exprcheck` pins it rather than
  fixing it, because fixing it changes what an existing file means — and
  `gencheck` pins the same list, so the two languages cannot drift apart on it.
- **A unit inside an expression is refused**, signal or not, and so is an
  expression on a `@chanarg` or on its range — a control is a constant the GUI
  writes, and a value with two authors is not a thing this format can express.
- **The editor draws one read-only box per expression** and refuses to wire
  into it. `disconnect` removes the whole thing by rewriting the arg to `= 0`.
  Deleting a control or a node the arithmetic reads replaces *that reference*
  and leaves the rest standing: delete `@detune` and `freq = freq->out +
  @detune` becomes `freq = freq->out + 1.3`, the value the control held. A node
  has no value to stand in and becomes `0`, as does a control whose value
  carries a unit, since no expression may hold one. See
  [NODE_EDITOR.md](NODE_EDITOR.md#expressions-are-one-box).

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

### `.step` and `.values`

```
@wave.step = 1;                       # this control means a whole number
@wave.values = "Sine,Sawtooth,Square";  # ...and here is what they are called
```

Both are overrides, and **no shipped file needs either**. The usual source of
this is the plugin on the far end of the control's wire: `osc::simple` declares
that it reads `waveform` as `switch ((int)x)` and names its six cases, and
[`thSynthTree::typeChanArgs`](ARCHITECTURE.md#arg-type) carries that to the
control. These two lines are for a control the plugin cannot know about, and for
overriding one it gets wrong.

`.values` is one string split on commas, spaces trimmed. An **empty entry is a
value with no name**, not the end of the list: `"Off,,High"` names 0 and 2 and
says 1 exists and means nothing — which is what `osc::window`, implementing
three of six waveforms, has to be able to say. A list implies `.step = 1` and a
range running from its first named entry to its last — so `"Off,,High"` declares
three indices, offers two, and its control runs 0..2. The length is a statement
in its own right: it says how many indices the arg has, which is not always how
many are worth offering.

The file's word is final, and that includes `@x.step = 0`. Saying it explicitly
is not the same as saying nothing, even though the value is the default —
`thArg::typedByFile` is what keeps them distinguishable, so a patch can hold a
control continuous against a plugin that would otherwise step it.

### The io node

The io node is the one node with no plugin, so nothing declares the direction of
its args. It is both the MIDI source (`note`, `velocity`, `trigger` are read by
other nodes) and the audio sink (`out0`, `out1`, `play` are written to it), and
any tool that treats it as one vertex will see a cycle in 89 of the 92 shipped
files that is not really there.

Direction can be recovered, because the engine's own use of the io node is
narrow. `thMidiChan::process()` reads exactly three things off it: `OUTPUTPREFIX`
plus a channel digit for the audio it mixes, `play` to learn the note has ended,
and `channels` to size the mix. `poly`, `mono` and `choke` are read once, at
construction — see below. Everything else travels the other way —
`thMidiNote` writes note, velocity and trigger, `thMidiChan` creates amp,
`thChanEffect` writes `in<N>`, and the author's constants are read by whoever
wants them.

**`aux0` to `aux3` are a note's own timbre.** A composed note carries four
floats beside its pitch, velocity and level (`thcEvent`'s `u.note.aux`), and
`thMidiNote` writes them into the voice's io node when the voice is built and
never again — a mono slide leaves them where they were, as it leaves
`velocity`. A graph reads them like `note`: `cutoff = @cutoff *
exp2(ionode->aux1)` is how `supersaw.dsp` takes a brightness per note. They are
written only into a graph that mentions them, so a file that reads none has
none. What each one means is the graph's to say, and a zero is a value — a MIDI
note, or one from a composer that never set them, carries zeros, so a graph
should read zero as "as patched". The convention the shipped graphs keep, and
`xform::vary` writes: `aux0` is a pan from -1 (left) to 1 (right), `aux1` a
brightness, `aux2` how slow the attack is, each -1 to 1 and 0 in the middle.

So an arg is an input to the audio-out half if

- the engine reads it — `out<N>`, `play`, `channels`, `poly`, `mono`,
  `choke`; or
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

### Voices: `poly`, `mono` and `choke`

Three io-node constants, read once when the channel is built, that say how
notes become voices. The first two were literals in the engine before they
were settings.

```
node ionode { channels = 2; poly = 2; mono = 1; out0 = vca->out; };
```

**`poly`** is how many voices the channel plays at once; without it, 10. Over
the limit the channel retires voices that are finishing first and then the
oldest still held, so what survives is always the newest. A `poly` of 0 — or
of anything negative — is no limit at all, which is what the engine's check
has always meant by a limit of zero.

**`mono = 1`** changes what a note *is*. A note arriving while another is
still held does not start a second voice: it retunes the one that is sounding
and the new voice is discarded. The retuned voice keeps its envelopes, its
filter state and the first note's velocity — only the pitch moves. Releasing
a key falls back to the newest key still down, which is last-note priority,
and the channel keeps a stack of the keys that are down to do it with.

A note arriving when *nothing* is held is an ordinary new voice, even if the
previous one is still in its release. That is the whole rule, and it is the
one a line already knows how to write:

> **overlap is a slide, a gap is a retrigger** — in a `.gen`, `hold` longer
> than `step` against `hold` shorter than `step`.

A voice the sustain pedal is holding counts as sounding, so it slides too, and
a key going down takes it back off the pedal.

`mono` on its own is a hard retune: the pitch steps. The glide is a
`misc::slew` on the frequency **inside the graph** — the voice outlives the
note that started it, so the lag's state carries across the retune and the
pitch slides into the new note. `dsp/bass.dsp` is that arrangement end to
end.

**`choke = 1`** is the drummer's pedal. A note arriving on a choked channel
sends *every* voice that is keyed into its release — whatever pitch it was —
and starts a fresh voice with a fresh attack at its own velocity:

```
node ionode { channels = 2; choke = 1; poly = 2; out0 = vca->out; };
```

That is not `mono`, which finds the voice that is sounding and retunes it
without re-attacking, and it is not the retrigger a second note at the same
pitch already gets: the open hat that has to die is a *different note* from
the closed one that kills it, so nothing a graph can reach does this. Which
voice to end is the channel's knowledge.

The voices it cuts are released, not silenced, so their tails run under the
new note's attack — which is what a hat pedal sounds like. `poly` is what
leaves room for that: a hat wants at least 2, one sounding and one finishing,
and at `poly = 1` the release is retired the moment the next note lands.
The choke applies to the whole channel; other drum instruments belong on
separate channels if they must not cut one another off.

`mono` and `choke` are exclusive and `mono` wins, because retuning the voice
that is sounding and cutting it off are opposite answers to the same question.

**A cut voice is marked.** `trigger` goes to **-1** rather than 0 on a voice
the choke ends — the same protocol the pedal's 2 is. Every envelope in the
tree tests `> 0` for held, so a negative trigger is a release exactly as a
zero is and no graph has to know about it. What it buys is the graph that
*does*: a one-shot drum holds its envelope's trigger at a constant, because
how long an open hat rings is its velocity's business and not the
sequencer's, and it still has to answer the pedal. That graph writes

```
node foot env::adsr {
    a = 0;  d = 0;  s = th_max;  r = @pedal;
    trigger = clamp(1 + ionode->trigger, 0, 1);
};
```

which is 1 for a key down, 1 for a key up, 1 for a voice the pedal is
holding, and 0 only for one the choke took. `dsp/hat.dsp`, `dsp/hat0.dsp` and
`dsp/hat808.dsp` multiply their output and their `play` by it, so a choked hat
goes quiet over `@pedal` and the voice retires when it does.

The top of `Pedal Close` gives a long release, not an off switch: an
overlapping hat still fades, and `poly = 2` still limits the channel to two
voices. An exact return to the old independent hats requires a graph without
the choke and foot gate.

### An effect graph

A `.dsp` whose io node declares **`in0`** is not an instrument. It is a graph
the engine runs on a *channel's summed voices*, once per window, and the `in0`
is where it puts them:

```
node ionode {
    channels = 2;

    in0 = 0;            # the engine writes these
    in1 = 0;

    out0 = mix->out;    # and reads these, as it does for a voice
    out1 = mix->out;
};
```

Nothing else about the file is different. The same nodes, the same
`@chanargs`, the same `out<N>`. `play` means nothing here — an effect never
ends — and neither do `note`, `velocity` or `trigger`.

**It is a different thing from an instrument and the two are not
interchangeable.** An instrument has no input; an effect has no envelope and
never finishes a note. `thSynth::loadEffect` refuses a graph with no `in0`,
`thSynth::loadTree` will happily load an effect and it will sit there
silently, and the note-playing harnesses (`dsplevel`, `dspsweep`, `dspprobe`)
skip a graph that declares `in0` and say so. `scripts/fxcheck` is where effect
graphs are covered, and § *Choosing a file* is how the distinction reaches the
chooser, which used to discover it only after the file was picked.

**What it writes replaces what it was fed.** The dry signal is the graph's to
mix:

```
node wet delay::echo { in = ionode->in0; delay = @delay; dry = 0; };
node mix mixer::fade { in0 = ionode->in0; in1 = wet->out; fade = @mix; };
```

which is one node more than a wet/dry control in the engine would be, and it
is a node the author can see and rewire.

**It runs every window, whether or not a voice sounds.** That is the whole
point: a delay's tail is exactly the part that comes out after the last
note-off, which is why `delay::echo` inside an instrument cannot be one — the
ring lives in the voice and the voice is gone.

**It may hear a second channel.** An io node that declares
`side0`…`side<N-1>` is given another channel's output there, every window,
beside the `in<N>` that carry its own:

```
node ionode {
    channels = 2;

    in0   = 0;          # this channel's voices
    side0 = 0;          # and the other channel's, if the piece named one
    side1 = 0;

    out0 = band0->out;
};
```

That is the carrier a vocoder needs and the kick a compressor is keyed from —
the two things an effect cannot do while it hears only the channel it is on.
Which channel it is comes from the piece rather than from the file: a `.gen`
says `effect "fx/vocoder.dsp" { side = carrier; };` (GEN_FORMAT.md §4b) and a
`.patch` writes a `side` line. The graph is the vocoder; what is being vocoded
is the piece's business.

The engine runs that channel **before** this one, so `side<N>` holds the window
being mixed and not the one before it. A channel that would end up waiting on
itself — directly, or around a ring of channels that each hear the next — is
refused when the effect is loaded. `side<N>` is read and never written back:
what an effect returns is its own channel's audio.

**Where no side was named, `side<N>` is this channel.** So a graph that reads
it always has a signal there — a compressor keyed from `side0` is an ordinary
compressor until a piece names a kick for it, which is why `fx/comp.dsp`
carries no knob for "is there a side". A side naming a channel with nothing
loaded on it *is* silence, because that is what an empty channel is putting
out; the two are different questions with different answers.

**And it may hear the machine.** An io node that declares
`live0`…`live<N-1>` is given what the host is capturing — a microphone, a line
in, whatever device was opened — there, every window:

```
node ionode {
    channels = 2;

    in0   = 0;          # this channel's voices, or the mix on a master effect
    live0 = 0;          # and what the machine is hearing

    out0 = band0->out;
};
```

That is the modulator a vocoder needs when the thing being vocoded is a person.
`dsp/fx/vocoder-mic.dsp` is that graph: the same sixteen bands as
`fx/vocoder.dsp`, with the modulator off `live0` and the carrier off `in0`, so
a piece wears whoever is in the room by naming one effect and nothing else.
`gen/voice.gen` is that piece — a string machine holding a chord, and its
header is the shortest way to find out what this sounds like.

Unlike a side it **names nothing**, because there is only ever one thing the
machine is hearing — so a graph asks for it by declaring it and no `.gen`
clause is involved. The capture is **mono**, so a graph that declares `live0`
and `live1` is handed the one signal twice, which is the rule `side<N>` already
follows for a mono side.

**Where no host is capturing it is silence**, which is also what a vocoder with
nothing to vocode should sound like — so a graph meant to be *opened* rather
than only played into wants some way past the bands, which is what
`fx/vocoder-mic.dsp`'s `dry` is for and why it defaults to 0. Every offline path — `genwav`, `gencheck`,
`dspcheck` — feeds nothing and therefore reads zeros, so a piece carrying a
live graph renders the same today as it did before one could. And a graph that
declares no `live0` renders bit for bit the same whether or not a host is
capturing: nothing is written where nothing was asked for.

**What it costs is one window, or two.** A host captures a device period and
the engine renders a window, so periods are accumulated into windows
(`src/gthSynthSource.h`), and the window handed out is always the one rendered
before — so a live graph hears one window late where the device period equals
the window, and two where it is smaller. In a browser the period is the
worklet's quantum of 128, which is why the page offers a window of 128 beside
its usual 256: 2.7 ms against 10.7.

**A microphone and speakers in one room is an oscillator.** The master limiter
saturates it rather than preventing it. Headphones.

**Its `@chanargs` are its own**, kept apart from the instrument's so that an
instrument's `@a` and an effect's cannot collide. From outside they are named
`fx.<name>`: `fx.delay` is the effect's, a bare `delay` is the instrument's.

**A graph may not contain a cycle.** Two nodes that read each other resolve as
a one-window delay — the walk clears each node's recalc flag before it
recurses — so what the file sounds like would depend on the window length,
and the window length is the audio device's business rather than the
author's. This has always been true and effect graphs are where it first
tempts anybody: a damped feedback path wants exactly that shape. Put the
filter outside the loop. `dsp/fx/echo.dsp` says so where it does it.

A channel's effect goes on **after** its instrument: loading an instrument
builds a new channel and the effect belongs to the channel it was put on.

**The same graph can run on the mix.** `thSynth::loadMasterEffect` puts one
after every channel has been summed and before the master gain and the output
limiter, where a reverb belongs — one room, rather than one per channel each
paying for its own — and where a limiter has to be, since what it limits is
the sum. Nothing about the file changes: `in0` is the mix rather than a
channel, the chanargs are the graph's own with no prefix at all (there is no
instrument on the mix to collide with), and the same refusal applies to a
graph that declares no `in0`. A `.gen` asks for one with a top-level `effect`
statement; see GEN_FORMAT.md §4b.

## 2. The `.patch` format

A `.patch` is **not** a graph. It is a reference to a `.dsp` plus flat
overrides — a preset over that DSP's `@chanargs`:

```
dsp ts1.dsp
side 3
effect fx/echo.dsp
info author Leif Ames
info title Phat Rip
cutoff 8.809662
res 2.854232
fx.delay 16537.500000
fx.mix 0.500000
```

`src/ArgPanel.cpp` describes these as panel rows and `src/gui/PanelView.cpp`
renders them as sliders.

`effect` names the channel effect — the graph that runs on the sum of this
patch's voices, above — and is optional; a patch without one is every patch
written before there were any. Its parameters are written `fx.<name>`, which
is how the whole engine addresses an effect's chanargs, so that a patch
setting `a` and an effect declaring one are two lines and two numbers.

`side` is the channel that effect listens to besides this one — the second
input described above — written 1-based, the way channels are numbered on the
mixer, and omitted where there is none. A number outside the rack is read as
no side: what is lost is a sidechain, and the instrument still plays.

**The order in the file is load-bearing.** An effect's parameters do not exist
until the effect is on the channel, so `effect` is written above them and the
reader depends on that rather than tolerating either order — and `side` above
`effect`, because the side is part of building the effect — a reader that
tolerated both would hide a writer that had stopped doing it. `dsp` comes
first for the same reason one step further back: an effect belongs to a
channel, and the channel is the instrument.

An unknown `fx.` name is reported and dropped rather than invented. The
tolerance for names no graph declares belongs to the instrument's side, where
the corpus has a history of them; an invented effect parameter would land in
the instrument's map, where nothing would ever read it.

### One reader

`src/PatchFile.h` is the format — a string in, a `thPatchDoc` out, and the
document back to a string — and nothing else reads or writes it. That is worth
saying because for a while two things did: `gthPatchManager::parse` over
`fgets` and a second parser in `wasm/web/patch.js`, written from this document
separately. They had drifted in both directions. The page dropped every
`effect` line on the floor, because it tried the value as a number; it sent
`side` to the engine as a chanarg called `side`; and it kept every value of a
`name 1,2,3` line where the desktop kept the first and dropped the rest.

That last one is the only place the page was right, and its reading is the one
that stands: an arg holds as many values as it was given, on both sides of the
file. No shipped `.patch` has a multi-value line, so nothing in the corpus
changed meaning — which `scripts/dspcheck --patch` is what proves.

### What a bad line does

It is complained about, and the rest of the file is read. The only thing that
fails a patch is the one thing that makes it not a patch: no `dsp` line.

| | |
|---|---|
| `info foo` — a property named, no value | complaint; no property is invented |
| `cutoff` — a word with no value after it | complaint |
| `cutoff abc`, `wave 1,,3` | complaint; the arg keeps what the `.dsp` declared |
| `cutoff nan`, `res inf` | complaint; `strtof` spells both, and neither is a value anything downstream can use |
| `cutoff 1.04 `, `wave 1, 2 ,3` | the blanks around a value are not part of it |
| a line starting with `#`, or blank | skipped, in silence |
| CRLF endings | the CR is not part of the value |
| `side 99`, `side` naming its own channel | read as written, then clamped to no side when it is put on a channel |
| `side` with no `effect` under it | read, kept and written back; there is nothing to put it on, but it is what the file said |
| `fx.` name nothing declares | reported and dropped when it is put on a channel |

A value that is not a number used to become a silent zero, because `strtof`
takes what it can and shrugs at the rest — a parameter set to a number nobody
typed, indistinguishable from one somebody meant. An `info` line with no value
used to fail the whole file, sharing an exit with a `.dsp` that would not
load, so a typo in a comment field cost the instrument.

Whether `99` is a channel and whether anything declares `cutoff` are questions
that need a synth, so they are answered when the document is put on a channel
and not when it is read. What the reader does is the format, and it
round-trips: parse, compose, parse again, and the second document equals the
first. `scripts/patchcheck` holds every shipped file and a fixture per row
above to that, and `wasm/web/patchcheck.mjs` holds the browser's reading
against the desktop's, byte for byte.

That the writing is one thing too is what makes a `.patch` saved in a browser
a file the application opens: the bytes come from `thPatchCompose` on both
sides, and all 101 shipped patches compose back to themselves byte for byte.

### A patch's category is its drawer

`patches/{bass,leads,pads,organs,brass,winds,drums/...}`. That directory is
part of the name — `leads/SuperRes.patch` is what `thinkrc` stores, what
`gthPrefs`'s first-run defaults spell and what the page's `patches/index.json`
lists — so it is load-bearing, and it is already the grouping every patch menu
draws.

There was also an `info category` line, edited through a box in the Patch
Selector, read by nothing. Twenty-six of the 101 shipped patches carried one
and **four of those contradicted their own drawer** (`patches/bass/FatRes.patch`
said `Leads`, and three more like it). That is the same failure `gen/README.md`
had, in the other direction: a categorization kept somewhere the thing it
describes cannot see it drifts.

So it is retired. The shipped patches no longer carry the line,
`scripts/patchcheck` fails one that does, and the box has gone from the Patch
Selector. The *reader* still accepts the property, because it refuses no
property and a `.patch` from elsewhere must keep round-tripping exactly.

**A patch does not inherit its graph's category either.** A `.dsp`'s category
describes graphs — *Synths*, *Drums*, *Plucked* — and a patch's drawer
describes what the sound is for; `bass/FatRes.patch` is a bass played on a
graph filed under *Synths*, and both statements are true. Two taxonomies over
two things, which is why neither is derived from the other.

## 3. Writing a `.dsp`

### Splice, do not re-emit

The parser throws away things a naive re-emit would not restore:

- **Comments.** 59 of 92 files have them, 391 in total, and they are the
  author's notes. Losing them on first save would be vandalism.
- **Units.** 283 values are written `5 ms` or `90%`. The loader folds these to
  raw floats, so `a = 5 ms` comes back as `a = 220.5` — correct, unreadable.
  (The lexer hands out `5` and `ms` as two tokens and the grammar keeps them
  that way; the fold happens once, at load. See below.)
- **Synthesised args.** `buildArgMap()` calls `setArg()` for every arg a plugin
  registered but the `.dsp` did not mention, with 0 or with the plugin's
  declared default. Re-emitting the in-memory model would write out dozens of
  `reset = 0;` lines nobody authored.
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
- `5 ms` is `5 * <sample rate> / 1000` and `50%` is `50 * TH_MAX / 100`. Both
  are exactly invertible, so a value written with a unit keeps it.
- **The fold happens at load, not at parse.** It used to be a grammar action
  using the compile-time `TH_SAMPLE`, which meant `thinksynth -r 48000` opened
  the device at 48k and then played every envelope in every patch 8.8% short.
  The grammar now records `(value, unit)` and `thSynthTree::foldUnits` converts
  once, in `finishParse`, at the rate the synth was built with. One record per
  *value site*, not per arg, because a file may write `@decay = 500 ms` and
  `@decay.max = 88200` — the same control, one site in milliseconds and one
  already in samples. `thUnits.h` holds the arithmetic, and the panel and the
  writer unfold through the same functions at the same rate; a `-r` session
  that displayed or saved through a different one would rewrite files to mean
  something else.
- **A unit inside arithmetic is a parse error.** `5 ms + 3` used to produce a
  number by accident — the leaf was folded before the operator ran, so it meant
  223.5 samples — and with the fold deferred there is no accident left to have.
  Nothing in the corpus does this; the rule exists so nothing quietly starts.
- **229 uses of `th_max` and `th_min`**, plus `th_range`, `th_midimax` and
  `th_sample`. A writer that did not recognise these would turn
  `inmax = th_max` into `inmax = 1` on the first save of any file containing one.
- **Arithmetic right-hand sides are refused.** An editor that silently replaced
  someone's `a * 2` with a constant would be doing exactly the damage splicing
  exists to prevent. That holds for an expression over signals too: the value
  the parse produced is a graph, and writing that graph back would be
  re-emitting the model with the author's arithmetic gone.
- **A label cannot contain a quote.** The lexer's string is `"[^"\n]*"` with no
  escapes at all, so there is no spelling for one.
- **`@x.min` before `@x` has nothing to modify** — the parser says so and
  ignores it. A control block is written value, widget, min, max, label, in that
  order, before the first `node`, which is where all 206 of the shipped ones sit.
  That order is the *only* constraint, so a metadata line added to a block that
  never had one goes at the end rather than in its canonical slot: reshuffling
  someone's file into the order this writer happens to prefer is an edit nobody
  asked for. A `.min` or a `.max` is a number like any other and every rule
  above applies to it: all 67 units on a range sit on a `.max` and are kept; a
  range spelled `th_max` is compared by value, so writing the 1 it means leaves
  it spelled `th_max`; and an arithmetic range is refused. No shipped file
  exercises the last two — all 412 range lines are plain literals — which is why
  `dspnew` writes the files that do.
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
the two are identical — `buildArgMap()` fills an absent arg with what the
callback already substitutes for 0 — but only the rewrite keeps the line's
position,
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

**Every shipped DSP declares a name, a description and a category.**
`mfm01.dsp` was the exception on the first two -- it called itself `test` and
said nothing else, while three patches and a shipped piece played it -- and
now has a header like the rest. `scripts/dspcheck --shipped` is the gate; see
§ *Choosing a file*.

**Every shipped DSP loads.** Eleven did not, until recently: nine in
`dsp/effects/` that read `input/wav` or `input/alsa` because they predated an
effect being able to hear a channel, and two in `dsp/old/` on `input/wav` and
`misc/wlan`. Those plugins compile and are deliberately not in the build —
`wav.cpp`'s own description string is `"Wav Input (BROKEN)"` — and all eleven
files have gone. `dsp/fx/` is where the effects idea lives now.

`cmake/RunHarness.cmake` still filters on what a file references, so building
one of those plugins would bring any file naming it back into the sweep. The
filter matching nothing is the point of it.

**All 101 patches load.** Two of them did not until recently:
`patches/pads/Rythmic.patch` and `Rythmic-2.patch` named an absolute
`/usr/local/share//thinksynth/dsp/mfm03.dsp` that was never in the tree, and
now name `mfm01.dsp`, which declares exactly the chanargs they set.

The CI gates therefore run over all 77 DSPs and all 101 patches, plus the
three specimens in `scripts/guard/`.
`cmake/RunHarness.cmake` filters DSPs by what a file *references* rather than
by name, so the exclusion cannot go stale. It strips comments before it looks,
which it did not always do: `dsp/fx/vocoder.dsp` explains in prose why it needs
a side channel rather than the `input::` nodes the graphs before it used, and
was quietly dropped from every sweep for saying so. The patch side once
filtered by name, on `Rythmic` — which also caught `ThickRythmic.patch` and
`ThickRythmic-2.patch`, two healthy patches on `ts2.dsp`, and dropped them from
the gate for as long as it stood. That is the argument for matching on content
in one line.

## 6. Choosing a file

Every shipped `.dsp` declares a `name` and (with one exception) a
`description`, and until recently no chooser read either: the desktop's Browse
opened a file chooser over `dsp/` and the page's menu listed filenames, so the
one moment a person has to pick a graph was the one moment nothing told them
what the graphs were. `rpiano0.dsp` and `rpiano1.dsp` differ by one character
and by which filter they run, which is a thing the description says and the
filename cannot.

Three pairs went further and shared a *title*: `bd10.dsp`/`bdshaped.dsp`,
`rpiano0.dsp`/`rpiano1.dsp` and `ts1.dsp`/`ts2.dsp`. A chooser draws that as
the same row twice, and `thSynth::loadTree` keys `treelist_` on the name and
deletes what was registered before, so the second to load evicted the first.
They are distinct now, and `scripts/dspcatalog` fails a corpus in which two
graphs share a title.

`src/DspCatalog.h` is what makes reading them cheap. **The header, not the
graph**: parsing a `.dsp` builds a `thSynthTree` and `dlopen`s every plugin it
names, which is not a thing to do seventy-seven times to draw a list. The
catalog runs the shared lexer (`libthink/thLexer.h`) over the file and picks up
the info statements, plus which node the `io` statement names, whether that
node declares `in0` — `thSynthTree::takesInput`, answered over the text — and
whether anything in the graph reads that node's `note`.

That last part is what lets **the effect split be enforced where the choice is
made**. An effect graph and an instrument are the same format and are not
interchangeable (§ *An effect graph*); the effect chooser offers the graphs
that declare `in0` and the instrument chooser offers the ones that do not,
rather than both offering everything and a dialog afterwards saying it was the
wrong kind.

The last of those is **whether the graph plays at the pitch it is sent**.
`note = ionode->note` somewhere in the file and it does; nothing reading it and
every note it is handed makes the same sound. `kick909.dsp` says so in its own
header — *the note number is ignored, a kick is a kick* — and that is a fact
about the graph rather than a taxonomy over it: the shipped **Drums** group
holds both kinds, and the difference between `kick909.dsp` and `tom808.dsp` is
that a tom is played at a pitch. `scripts/dspcatalog` gates the corpus on it
holding both, so a flag that quietly became *is this filed under Drums* fails.

What reads it is the browser's sequencer. A track's grid is as tall as its
`rows` param, and a ladder over an instrument that ignores the note is six rows
that make one sound — so a track aimed at a kick is set to a single row, and
one aimed at a tom keeps its ladder. A chooser could use it too and none does
yet.

An entry is named the way a file names it — `ts1.dsp`, `fx/echo.dsp` — because
that is what `thUtil::findDataFile` resolves, what a `.patch`'s `dsp` line
says and what a `.gen`'s `dsp` clause says. Choosing from the catalog therefore
puts the short name in the document rather than this machine's absolute path.

Entries are grouped by their `category` statement; a file that declares none
falls back to the directory it was found in (`fx/` is Effects) and then to
Uncategorized. A category is optional, and that fallback is the difference
between a category and a schema.

### `category`

`category "Drums";` beside `name`, `author` and `description`. It is a
statement rather than a `# @category` comment because the other two structured
comments — a layout and a probe — are the editor's business and the engine has
no use for them, where this is a fact about the file. `thSynthTree::category()`
has it, nothing derives it from anywhere else — a `.patch` does not inherit
its graph's, for the reason given above — and there is one place it lives.

**The word is reserved.** `.dsp` keywords are hard: the lexer returns `CAT` for
`category` wherever it appears, so no graph may use it as a node or an arg
name. Nothing in the corpus did when this was added and the reservation is
permanent after — the same price the format already paid three times, for
`name`, `description` and `author`. (`.gen`'s keywords are contextual and its
`category` reserves nothing; GEN_FORMAT.md § 7a says why the two differ.)

**Free text in the format, a list the shipped corpus is gated against.**
`scripts/dspcheck --shipped` fails a graph in this tree that declares no
category or one outside the nine: *Bass*, *Drums*, *Effects*, *Experiments*,
*Keys*, *Leads and stabs*, *Plucked*, *Strings and pads*, *Synths*. The flag is
what separates "the corpus" from "a `.dsp`" — the fixtures in `scripts/guard`
are swept without it, and a graph of your own may say whatever it likes and
lands in its own group.

The arguable placements are arguable in one direction each. `waveguide.dsp` and
`guitar.dsp` are plucked strings and get *Plucked* rather than being filed as
leads; `fircomb.dsp`, `spectral.dsp` and `sandh.dsp` were promoted out of the
old drawers because they are interesting rather than because they are useful,
and *Experiments* says so where *Synths* would not.

Nothing writes the statement yet but a text editor. The node editor cannot
write `name`, `author` or `description` either — there is no header-editing
surface in it at all — so this is one field short of a feature rather than a
field left out of one.

`scripts/dspcatalog` holds the two readings together: every shipped file is
scanned *and* parsed, and the title, the description and the kind have to
agree. What a chooser offers — the kind split, the filter — is
`DspCatalog::matches` rather than a rule inside a widget, so it is checked
there too, with no display anywhere near it.

**The page reads the same headers.** A worklet cannot fetch, so the page hands
every shipped graph to the module before the first piece loads
(`tw_instrument`) — which means that by the time a menu is drawn there *is* a
directory to walk, in MEMFS, and `tw_dsps_json` walks it with this class.
`wasm/web/dspcatalogcheck.mjs` diffs that dump against
`scripts/dspcatalog --json` byte for byte. That is the gate the `.patch`
format did not have while it was being read two different ways, and it is why
`patch.js` no longer parses anything.
