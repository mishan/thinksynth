# The two front ends

thinksynth has two front ends: the desktop window (`src/gui`, GTK4) and the
browser page (`wasm/web`). They share the engine and the composer's C++
(`ComposerCanvas`, `PatchApply`, `thcScheduler`), not their widgets. This
document lists what each one shows and the rules both have to follow, so a new
control is designed once and then built twice to the same rules. Where the two
still differ, it says whether that is on purpose or not done yet.

## 1. Modes

| Desktop | Browser ("Play") | What drives the channels |
|---|---|---|
| Patch | a patch | the keys, on one patch |
| | a patch, growing | the keys, on a patch the grower is changing |
| Sequence | a sequence | tracks the person draws |
| Piece | a piece | a `.gen` piece |
| | a band | a piece, shown as its players |

Growing and the band exist only on the browser page, on purpose for now. Both
front ends save a layout for each mode and put it back when you return to that
mode.

## 2. Panes

Both front ends use the same pane ids, so a saved layout means the same thing
on each. A pane the current mode doesn't have is *unavailable*, not closed: it
leaves the layout but keeps its place for when that mode comes back.

| id | Desktop title | Browser title | Modes |
|---|---|---|---|
| `channelbox` | Channels | Channels | desktop: all; browser: sequence, piece, band |
| `paramview` | Patch params | Patch params | all |
| `nodeview` | Patch graph | Patch graph | all |
| `keyboard` | Keys | Keys | all |
| `seqview` | Sequencer | Sequencer | sequence, piece |
| `roll` | Piano roll | Piano roll | sequence, piece, band |
| `composerview` | Piece | The piece | piece |
| `pieceedit` | Piece settings | - | piece |
| `selection` | Selection | - | piece |
| `patches` | Patch Selector | - | all |
| `midimap` | MIDI routing | - | all |
| `controller` | Controller | - | all |
| `knobs` | - | The piece's knobs | piece, band |
| `piecesource` | - | Piece source (.gen) | sequence, piece, band |
| `patchsource` | - | Patch source (.dsp) | patch, growing |
| `growview` | - | Grow | growing |
| `bandview` | - | The band | band |
| `packs`, `videoview`, `cameraview` | - | Sample packs, Video, Camera | all |
| `detail` | - | Debug | all |

Panes on only one side:

- **On purpose.** Debug, the two source panes, and Sample packs exist only on
  the browser page: a desktop user has the files and a terminal. The Patch
  Selector exists only on the desktop, which loads and saves patch files on
  disk.
- **The same job, done differently.** The desktop's MIDI routing and
  Controller are panes of their own; on the browser page they are sections
  inside Keys. The desktop edits a stage in Selection; the browser page opens
  a popover over the canvas. The desktop's Piece settings (the piece, its
  knobs, scales, presets and MIDI) are the browser page's knobs, with the rest
  in the piece source.
- **Not decided.** On the desktop, Channels is in every mode; on the browser
  page, patch modes have none because they play one patch on channel 1.

Default layouts are data: `PATCH_LAYOUT`, `SEQ_LAYOUT` and `PIECE_LAYOUT` in
`src/gui/MainSynthWindow.cpp`, and the `*_LAYOUT` constants in
`wasm/web/main.js`. The sequence layouts are the same tree, except that the
browser page puts the piece source as a tab behind Channels. The patch layouts
differ: the browser page has no Channels there, and adds the patch source and
Debug. So do the piece layouts: the desktop opens on the canvas with Piece
settings and Selection beside it and Keys behind the roll, the browser page on
the sequencer with a column of the piece's knobs, Channels, the piece source
and Keys.

## 3. The current channel

A **current channel** is what Patch params, the tuning control and the keys
show and play, on both sides. So does Patch graph on the desktop. On the
browser page Patch graph shows the file chosen in its File list, live on the
channel that file plays on (the first instrument naming it); following the
current channel is **not done yet** (section 7). A pane that shows "the
channel" reads the current channel and has no channel selector of its own. It
has one writer: `MainSynthWindow::selectChannel` on the desktop, `pickChannel`
on the browser page. Every source below calls that writer; the browser page's
mode switch restores the mode's own channel without one, as it is not a new
pick.

| Source | Channel |
|---|---|
| A Channels row, the keys' channel control | the one picked |
| A press on a sequencer track, or a key its grid uses | that track's notes channel, from `ComposerCanvas::channelOf` |
| A press on a canvas chain, stage or notes sink | its notes channel from the same rule; nothing for an instrument that plays over MIDI out |
| A sequence loaded | the last track's channel; this pick doesn't count as the person's own choice |
| A piece loaded where the keys can't be heard (browser page only) | a channel the piece listens on; not the person's choice either |

These never change it: focus, incoming MIDI, playback, the HID controller, a
rebuild, a canvas knob, a room peer's edit, a press on Patch graph's own
canvas or palette (the editor is attached to a channel; it never picks one).
Every track on the current channel is marked with a pointer before its channel
number, and its accessible description says so.

On the browser page each mode remembers its own current channel, so a choice
made in sequence mode is still there after a visit to piece mode. Patch and
growing stay on channel 1. On the desktop there is one current channel across
all modes; that is **not done yet** (section 7).

A canvas pick that arrives late (the browser page asks the engine in a worker)
is dropped if the person picked again since, changed mode or loaded another
piece. Each press carries a counter (`pickSerial`) for this.

## 4. The keys

The keys (on screen and the computer keyboard) play the current channel:

- If the piece listens on that channel (`input midi`), the keys go into the
  piece, as a MIDI device would.
- Otherwise they play the channel's instrument directly.
- A note is released along the same route it was pressed on.

So a key is never silent on a channel that has something on it. The browser
page does this. The desktop plays the instrument directly and adds the piece
on top only when **Kbd input** is on; that is **not done yet** (section 7).

## 5. Tuning

A tuning is a Scala scale plus an optional keyboard map, set on a channel.
It's saved in the patch (`tuning` lines, [DSP_FORMAT.md](DSP_FORMAT.md)) or in
a piece's instrument ([GEN_FORMAT.md](GEN_FORMAT.md) 4c). The control is in
Patch params. It shows "Tuning: " followed by the scale's name, "a scale" for
one with no name, or "12-TET", and offers 12-TET, a scale (.scl), a keyboard
map (.kbm), and No keyboard map. All four are always there; 12-TET and a
keyboard map are enabled only on a tuned channel, and No keyboard map only
when there is one.

- **Retuning is live.** A new table applies to the next note. A held key
  finishes at the pitch it started at, so the patch is never rebuilt. A mono
  voice sliding to a new key takes that key's table.
- **The piece's tuning is the piece's.** On a channel a piece filled, the
  control is disabled (the desktop greys out the tuning's name, the browser
  page says "set by the piece") and you change it in the `.gen`. A sequence's
  instruments are the person's, so their channels can be tuned.
- **A tuning stays with its patch.** Reloading the same patch keeps the
  tuning: Revert, Load of the patch in view, the graph editor writing it, grow
  keeping a candidate. So does a sequence rebuilt over the same `.dsp`, for
  example after an edited value. Choosing a different patch is a new
  instrument and starts in 12-TET, unless that patch has its own tuning. On
  the browser page, entering patch mode starts in 12-TET too.
- **A late answer is refused.** On the browser page, if a file chooser is
  answered after the channel, the patch, the mode or the piece changed under
  it, or while a piece is loading, the tuning isn't applied. The desktop
  checks only that the patch it was opened for is still loaded and still not
  the piece's; it then tunes that channel even if another is current.

## 6. Edits and Undo

The editor's own behavior is in [NODE_EDITOR.md](NODE_EDITOR.md) 3 and 6;
these are the rules across panes.

- **Every edit is one step in its own pane's history**: the patch graph's
  (`NodeHistory` on the desktop, `TextHistory` per file and mode on the
  browser page) or the piece's. Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y act on the
  pane with focus; a text field outside it, like the palette's filter, keeps
  them for its own typing.
- **In a room, Undo takes back only this page's own steps** (`roomundo.js`). A
  step that would leave text that doesn't parse is put back and said, then
  skipped if asked again.
- **A rebuild never loses an unsaved edit or its history**, and replacing what
  a pane edits asks first where there is something to lose. The desktop holds
  a changed graph editor across a page rebuild and asks before Open, Revert or
  Change DSP discards it. The browser page has no Save, since the text is the
  file, so there the rule is only that a rebuild keeps the history.
- **A number beside a slider has no stepper buttons**; a number with no slider
  keeps them. The browser page's parameter panel still shows native spinners;
  **not done yet** (section 7).

## 7. Not done yet

- Desktop: one current channel per mode, as on the browser page.
- Desktop: the keys follow section 4's rule, and Kbd input goes away.
- Desktop: a piece the keys can't be heard in aims them where they can.
- Desktop: a tuning answer is refused on the browser page's terms.
- Both: the Keys pane as a short strip, with the device setup moved out of it.
- Browser: Patch graph follows the current channel, as Patch params does,
  instead of its own File list.
- Browser: no stepper buttons on a parameter's number box.
- Both: the patch editor (graph, params, the header strip) opened for the
  current channel from any mode, instead of from patch mode only. Before that,
  it needs rules for:
  - a piece's channel: Change DSP and Open replace what the `.gen` declares,
    so they refuse there as tuning does; editing the `.dsp` the piece names is
    editing the instrument's own file;
  - one `.dsp` on several channels: a value moved live reaches only the
    attached channel, and a structural edit reloads every channel playing the
    file, cutting their notes while a piece plays (and for every peer in a
    room);
  - history: the desktop's starts over when another file is opened, so moving
    between channels would discard Undo each time; it needs one per file, as
    the browser page has, and the browser page's drill-in from piece mode
    keeps piece mode's history for a name patch mode also uses;
  - a channel playing a shipped patch, which the browser page can't edit
    without copying it into its own files first.
- Engine: on a channel the keys and the piece share, their notes can cut each
  other off.

## 8. A new control

Before building a control on either side:

1. Which pane does it go in? Use the same pane on both sides, or add a row to
   section 2 saying why it's only on one.
2. Does it show or set something per channel? Then it reads the current
   channel and is told when that changes. It never has a selector of its own.
3. Does it set something a piece owns? Then it refuses on the piece's
   channels, as tuning does.
4. Does it take an answer later (a file chooser, a worker)? Then it checks
   that the channel, the patch load (the slot's generation, not its file name,
   which a Save As changes), the mode and the piece are still the ones it
   asked about.
5. Is it an edit? Then it is one named Undo step in its pane's history, and in
   a room only the page's own.
6. The browser page's `pagetest.mjs` and the desktop's `windowcheck` each get
   the same check, written against the same rule.
