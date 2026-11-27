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

Each mode remembers its own current channel, so a choice made in sequence
mode is still there after a visit to piece mode. On the browser page patch
and growing stay on channel 1; on the desktop patch mode has Channels and
keeps whichever was picked there, and a mode not visited yet starts on the
channel of the mode it was entered from.

A canvas pick that arrives late (the browser page asks the engine in a worker)
is dropped if the person picked again since, changed mode or loaded another
piece. Each press carries a counter (`pickSerial`) for this.

## 4. The keys

The keys (on screen and the computer keyboard) play the current channel, and
the hardware controller plays its channels the same way:

- If the piece listens on that channel (`input midi`), the keys go into the
  piece, as a MIDI device would.
- Otherwise they play the channel's instrument directly.
- A note is released along the same route it was pressed on.

In patch mode the keys always play the instrument. A key the piece takes is
heard only as the piece answers it: a chain that waits for its next tick,
an arpeggiator say, is silent while the transport is stopped.

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
- **A late answer is refused.** If a file chooser is answered after the
  patch on the channel was replaced, or the current channel or the mode
  changed (even and back again), the tuning isn't applied. The browser page
  also refuses one answered while a piece loads; the desktop loads a piece
  in one step on the GUI thread, so no answer can arrive during one.

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

- Desktop: a piece the keys can't be heard in aims them where they can.
- Both: the Keys pane as a short strip, with the device setup moved out of it.
- Browser: Patch graph follows the current channel, as Patch params does,
  instead of its own File list.
- Browser: no stepper buttons on a parameter's number box.
- Both: the patch editor (graph, params, the header strip) opened for the
  current channel from any mode, instead of from patch mode only. Decided:
  - **One action**, Edit instrument, on anything with a channel (a Channels
    row, a track, a canvas chain, the keys). It picks the channel through the
    one writer and brings Patch params and Patch graph into view where they
    are. A pane the mode's layout has no place for opens, the first time, in
    a slot the layout data names. No temporary layout and no mode switch: the
    piece keeps playing.
  - **Three owners.** Which `.dsp` is on a channel is the piece's on its
    channels, so Change DSP and Open refuse there ("set by the piece"), and
    the person's in sequence and patch modes. The `.dsp` text is the file's,
    editable from any mode. Patch params values are the channel's.
  - **An edit reaches every channel playing the file.** A control or value
    moved in the graph goes live on each channel whose slot names that file
    (compared as resolved paths, since a slot keeps the name as written),
    except a channel whose patch sets that arg itself; that one keeps its
    value and the graph's header says so. The header names every channel the
    file is on.
  - **A structural edit reloads at once**, on every channel playing the file,
    in any mode. Held notes finish on the graph they started on
    (`thMidiChan::drain`), so there is no wait for a bar line. The browser
    page reloads only in patch mode today; piece and sequence modes need the
    same reload after the file is written. In a room every peer reloads when
    the shared text changes.
  - **History per mode and file** on both sides, so moving between channels
    never discards Undo.
  - **A file the page can't edit** (a shipped patch): in patch and sequence
    modes the drill-in offers to edit a copy, which the person then owns; on
    a piece's channel there is no copy, since it would change the slot.
  - **The File list stays** for the piece's files that no channel plays yet.
  - **A reload keeps the channel's Patch params values**, saved or not, as
    the patch's own overrides are kept; on each side, whether it does today
    is to be checked.
  - **Patch mode stays a mode**: growing and the one-channel patch layouts
    hang off it.
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
