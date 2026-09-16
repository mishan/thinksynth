# Aiming a channel — a bug in the page, and the fix

What sounds on a channel in the browser page depends on what the page did
earlier, not on the piece. Two shipped pieces show it, one of them
differently on two machines, and the report read as a platform problem.
It is not. This is the bug, why the desktop application never showed it,
and what the page should do instead.

## 1. What was seen

- `fern.gen` plays silently in every browser. The roll fills with notes.
- `hands.gen` sounds on a desktop browser and is silent on a phone, with
  the roll showing the arpeggiator's figure on both.

## 2. What is happening

Neither piece declares an `instrument`. Their sinks name channels, and the
comment in each file says so: fern asks for channel 4 aimed at something
plucked and 3 at a soft pad; hands asks for 1 plucked, 2 keys, 3 a pad.
Aiming is the reader's job, and `scripts/genwav` says as much about a
piece whose sinks name channels: it renders them silent, "since nothing
is loaded on them". Rendered natively for two minutes, fern delivers 3460
notes at a peak of 0.000.

The page never loads anything on a channel a piece names. It has two
modes. Patch mode puts the chosen `.dsp` on channel 0 when Start is
pressed and whenever the patch changes. Piece mode loads a piece, which
loads that piece's instruments and nothing else, and leaves every other
channel as it was; the mode switch's own comment says the other mode's
patch is "still on the channels until it does".

So hands sounded on the desktop browser by accident. Start in patch mode
put `ts1.dsp` on channel 0. Switching to piece mode loaded hands, which
claimed no channel, so channel 0 kept ts1. The keys go to channel 0, hands
listens there, the arpeggiator's figure comes back out on channel 0
through ts1. The corrected and shadow chains, on channels 1 and 2, were
silent on the desktop too, under the arpeggio.

On the phone that did not happen: piece mode was selected before Start,
or the browser restored the mode from last time, so the piece loaded
first and channel 0 was never given a patch. Composing needs no
instrument, so the roll showed the figure and the speaker played nothing.

Fern's sinks are channels 3 and 2 in the engine's numbering, a file's
`channel = 4` being the loader's 3, and no mode ever loads anything there.

### Why the desktop application never shows it

`gthPrefs::LoadDefaults` puts four patches on the first four channels at
first run: `leads/SuperRes`, `bass/FunkMachine`, `organs/Organ1`,
`pads/SynString`. Every channel fern and hands name is one of those four.
The application has an answer for an unaimed channel and the page has
none; the pieces were written against the application.

### A confound, already fixed

On the deployed site the key-patch menu's default, `rpiano0.dsp`, was
returning 403, because the file is mode 640 in the source tree and the
build's copy kept that. "Put it there" loaded an HTML error page as the
patch and failed to parse. The `dist` target installs every file 644, so
this is gone; it is recorded here because it made the phone look worse
than it was while the real cause was the one above.

## 3. The rule

**What a channel sounds like is decided by the piece and, where the piece
is silent on the matter, by the page's defaults. Never by what the page
did before.**

That is the application's rule, and the page should have it: the same
piece sounds the same on any device in any order of clicks.

## 4. The fix

### 4.1 The engine says which channels a piece uses

`tw_listens(channel)` already answers for `input midi`. Two exports join
it:

```
tw_sink_count()           how many distinct channels the loaded piece's
tw_sink_channel(k)        sinks name that no instrument of its own occupies
```

Both note sinks and `chanarg` sinks count: a chanarg sink writes a knob
of whatever is on the channel, and needs something there as much as a
note does. A channel an instrument block took is not in the list; the
piece has aimed it.

```
tw_chanarg(channel, name, values, count)
```

sets a chanarg on the tree loaded on a channel, through `setChanArg`, the
path the application's slider uses. A name the tree does not declare is
ignored and reported once, as `tw_knob` does for a knob.

### 4.2 The page loads `.patch` files

A `.patch` is a `dsp` line, `info` lines, and flat `name value[,value]`
overrides for that DSP's chanargs (DSP_FORMAT.md section 2). `patch.js`
parses one, loads the named `.dsp` on the channel with `synth.load`, then
sets each override with `synth.chanarg`. That is what
`gthPatchManager::parse` does, in the same order, at the same level
(`TH_DEFAULT_CHAN_AMP`, which `tw_load` already applies).

The `patches/` tree joins `dsp/` and `gen/` in the build and the dist, with
an `index.json` listing them by their relative names, `leads/SuperRes.patch`
and so on, the names the application's `thinkrc` uses. The `.dsp` a patch
names is looked up in the texts the page already fetches at Start.

### 4.3 Defaults, the application's

A table in `main.js`, the same four as `gthPrefs.cpp`:

```
0  leads/SuperRes.patch
1  bass/FunkMachine.patch
2  organs/Organ1.patch
3  pads/SynString.patch
```

A channel `c` above 3 takes the entry at `c mod 4`, so a piece naming
channel 7 sounds rather than not. The application leaves channels 4 to 15
empty; the page has no first-run file to edit, so it should not.

### 4.4 When a piece loads

After `tw_piece_load`, for each channel in `tw_sink_channel`:

- if the page has aimed it by hand in this session, keep that;
- otherwise load the default for it.

Channels the piece's instruments took are the piece's and are shown as
such. Channels the piece does not name are left alone; whatever is on
them is harmless.

The order matters and is the reverse of today's accident: the piece
first, then the aiming, so the aiming is a function of the piece.

### 4.5 The channels row

Replaces "Keys into channel N with X, Put it there". One line per channel
the piece touches, which is the union of its sink channels, its `input
midi` channels, its instrument channels, and the key channel:

```
1  SuperRes       [patches and .dsp files, or nothing]
2  FunkMachine
3  (pad, the piece's)
```

Shown in the file's numbering, one up from the engine's, since that is
what a person reading the piece's comment will look for. The key channel
selector stays, and the key channel's own line is where its instrument is
chosen; "Put it there" goes.

Patch mode is unchanged: one `.dsp`, channel 0, the text box.

### 4.6 The room page

Aiming is per page. It is not in the document and not on the tape, so two
peers may hear fern's channels through different instruments, which is
exactly the situation two people with two `thinkrc` files are in. M3 lives
with that. The durable answer is a piece that carries its instruments,
which the document then shares, and a later action can write an aimed
channel back into the piece as an `instrument` block.

## 5. The gate

`piececheck.mjs` gains one check: every shipped piece, loaded the way the
page loads it, with the defaults of 4.3 applied to the channels of 4.1,
renders a non-zero peak within its first minute. A piece fed by `input
midi` gets a chord held through `tw_midi_on` for the check. A shipped
piece that is silent under the page's defaults fails the build, which is
the property the report was missing.

## 6. The alternative

Give fern and hands `instrument` blocks and leave the page alone. Both
would then sound anywhere, and the file would say what it sounds like,
which is better than a comment asking the reader to aim. Worth doing for
fern regardless. It is not the fix, because the format allows channel
sinks, the application has an answer for them, and the next piece written
that way would be silent in the page again.

## 7. Order of work

1. `tw_sink_count`, `tw_sink_channel`, `tw_chanarg`; the worklet messages
   and `host.js` calls for them.
2. `patches/` and its `index.json` in the build and the `dist` target.
3. `patch.js`: parse and load.
4. The defaults table, the aiming on load, the channels row.
5. The gate in `piececheck.mjs`.
