# The thinksynth SDK for the browser

`thinksynth.js` is the engine for a page that is not thinksynth's own: a
game, a toy, a visualizer. It loads a piece and the instruments it names,
plays it in an AudioWorklet, tells the page what is about to be played
before it sounds, and lets the page change the piece while it plays
without that foresight going wrong.

It is a thin layer over `host.js`, the same one the site's own pages use.
What it adds is the decisions every such page otherwise has to make for
itself, and gets wrong in the same few ways: which files a piece needs,
where transport zero is, the difference between what is rendered and what
is heard, and when a change may land.

## Building it

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S wasm/web -B build-web
cmake --build build-web --target sdk
```

`build-web/sdk/` is the package: the modules, `thinkweb.wasm`, every
shipped graph and sample under `dsp/`, every shipped piece under `gen/`,
and a `package.json` naming it `@thinksynth/web`.

Serve the directory whole, as static files, and import `thinksynth.js`
from where it is served. The worklet (`worklet.js`), the look-ahead and
ear workers (`lookahead.js`, `ear.js`, `evolve.js`), the modules they
import and the `.wasm` files are each loaded by URL relative to
`thinksynth.js`, so a bundler that rewrites or moves `thinksynth.js` and
leaves the rest behind breaks them: keep the directory out of the bundle,
copied as it is (Vite's `public/`, for one), and import `thinksynth.js` by
the URL it is served at. `dsp/` is fetched from the same directory unless
`assets` (below) says where else it is served; `gen/` is only there for
the page to fetch pieces from.

## A piece, played

```js
import { createEngine } from './sdk/thinksynth.js';

const ctx = new AudioContext({ latencyHint: 'interactive' });
const engine = await createEngine(ctx);

await engine.load(await (await fetch('gen/outrun.gen')).text(), {
    voices: { lead: 'dsp "analog.dsp"; amp = 18;' },
});

engine.on('ahead', (notes) => road.add(notes));
engine.on('played', (notes) => flash(notes));
engine.on('end', () => showCredits());

await engine.start({
    fromBar: 8,
    setup: (e) => e.mute('hats'),
});
```

`createEngine(ctx, options)`:

| option | |
|---|---|
| `assets` | where `dsp/` is served; the SDK's own directory if not given |
| `bars` | how far ahead the look-ahead composes; 2 if not given (below) |
| `output` | the node the engine plays into; `ctx.destination` if not given |
| `onLog` | what the engine and its workers have to say |

`load(text, { seed, voices })` fetches the graphs the piece names
(`dsp "..."` and `effect "..."`, outside `#` comments) and the samples
those graphs name, and nothing else. A load waits for the edits asked for
before it, and an edit asked for after it is made on the new piece. `voices` adds instruments of the page's own — the body of an
`instrument` block, by name — on channels no chain plays, for `play()`.

`start({ fromBar, setup })` starts at a bar. Anything `setup` changes is
stamped at the start itself, before anything is composed past it, so a
piece can begin with parts muted. The piece's tempo is taken to stay put:
everything below that converts between seconds and bars does so at the
tempo the piece was loaded with.

## Time

Every time the SDK takes or gives is transport time, in seconds. There is
none before the first `start()`: until then the calls below throw, except
`barAt` and `sectionAt`.

| | |
|---|---|
| `heard()` | what is coming out of the speakers now |
| `rendered()` | what the worklet is about to render; a note stamped past it plays on time |
| `written()` | how far the look-ahead has composed |
| `line()` | where a change to what is composed lands if it is made now |
| `barAt(t)`, `sectionAt(t)` | the bar, and the section, a time falls in |
| `beatSeconds`, `barSeconds`, `tempo`, `meter` | the piece's |

Draw from `heard()`. Judge what the player sees against `heard()` too: the
note on screen is the note in their ears. `rendered()` runs ahead of it by
the output latency, which is anything from a few milliseconds to a fifth of
a second on Bluetooth, and is for stamping.

## Seeing ahead

A second, silent instance of the module — the look-ahead, `lookahead.js` —
is fed every command the worklet is fed and runs ahead of it. What its
scheduler delivers arrives as `ahead` events: the notes the worklet will
play, a bar or two before it plays them, each `{ at, duration, note,
velocity, chain }`. The same notes arrive again as `played` events when the
worklet delivers them.

The catch is that a change to what is composed changes the future the
look-ahead has already written, and a scheduler cannot be rewound. So the
look-ahead never steps past a line a change could still land on: **every
change to what is composed is stamped at the start of the bar `bars` past
the one being played, and the look-ahead composes up to that line and no
further** (`horizon.js`). Both work the line out from the same frame, the
one in the worklet's latest tape batch, and the page hears of that frame
in the message that steps the look-ahead, so a change posted now always
reaches the look-ahead before it can pass its stamp. Nothing is ever
rewound, and what `ahead` says is what is played.

The cost is that a change lands between `bars - 1` and `bars` bars after it
is made: with the default of 2, on the downbeat after next. That is also
when a band would bring a part in.

Everything that changes what is composed goes through the line:

| | |
|---|---|
| `mute(chain, on)` | a chain's live mute; a muted chain delivers no notes |
| `knob(name, value)` | a piece knob (`@name`) |
| `addStage(chain, stage, plugin, params, { category })` | a stage at the end of a chain; `xform` if not given |
| `removeStage(chain, stage)` | takes one out, by name |
| `setParam(chain, stage, param, value)` | one param of one stage |

Each takes `{ at }` to stamp it later than the line; earlier throws (an
edit's promise rejects), since the look-ahead may have composed past it.
The three edits are made one at a time, each on the text the one before
left, and each returns a promise of the time it was stamped at, which
rejects if the edit was refused or its `at` has fallen behind the line by
the time its turn comes. All of them throw while the engine is not
playing, before `start()` and after `stop()` or `end`, except inside
`setup`: a stamped change waits for a running transport.

What does not change what is composed may go at once:

| | |
|---|---|
| `knob(name, value, { now: true })` | a knob only an effect or an instrument reads |
| `play(instrument, note, { velocity, duration, at, quantize })` | a note on an instrument no chain listens to; `quantize` in beats |

`play()` too needs the engine playing, and throws otherwise; a knob with
`now` does not. `now` is the caller's word that the knob composes nothing. A knob bound to
a composer stage's param and moved with `now` makes the look-ahead and the
worklet disagree from there on.

## Under it

The SDK is `host.js` and the module the site's pages use; `engine.synth`
is the `createSynth` it was built on, for anything this page does not
cover. The look-ahead is `silent.js`, which the site's mirror is built on
too.

## What it does not do yet

- **Tempo changes.** Bars are converted at the tempo the piece loaded with.
- **Which knobs compose.** `now` is the caller's claim; the module could
  say which knobs are bound to a composer stage and refuse it for those.
- **Live levels.** A chain is muted or not; there is no live per-chain
  level, so ducking and fades need a section or an effect knob.
- **Muted notes.** A muted chain delivers nothing, so a page cannot show
  what is not being heard.
- **A chain's channel.** The module's chain list reports a sink's channel
  as written, so the SDK maps chains to channels through instrument names.
- **Closing.** `ctx.close()` stops the sound; the look-ahead's worker stays
  until the page goes.
- **Arrangement cues.** Sections that hold until cued are on a branch of
  their own; once in, a cue knob goes through the line like any other.
