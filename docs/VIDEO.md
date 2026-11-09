# Video

thinksynth draws moving pictures the way it makes sound. A video synth is
an ordinary synth whose sample clock is the pixel clock: each sample is one
pixel of a raster, row by row, left to right, so the nodes that make sound
make pictures. An oscillator at the note's pitch draws horizontal bars; a
sine of the distance from a point draws rings; anything finer than the
pixels draws moire. It plays a piece's notes and hears its music, and a
render is the same bytes every time.

Offline for now: `genwav` renders a piece's audio and its video side by
side.

## Rendering

From the source tree, with the build in `build/`:

```
build/scripts/genwav -p build/plugins/ -s 60 -o ebb.wav \
    --video video/moire.dsp --video-out ebb.y4m gen/ebb.gen
```

| option | |
|---|---|
| `--video DSP` | the video synth's graph |
| `--video-out FILE` | where its frames go, as a `.y4m` |
| `--raster WxH@FPS` | the picture: 160x120@60 unless given |
| `--video-follow A,B` | the instruments whose notes it plays; every one unless given |
| `--video-fx DSP` | an effect on the whole picture |

mpv plays the `.y4m` as it is. To put the sound with it:

```
ffmpeg -i ebb.y4m -i ebb.wav -vf scale=640:480:flags=neighbor \
       -c:v libx264 -pix_fmt yuv420p -colorspace bt470bg \
       -color_primaries bt470bg -color_trc gamma28 -c:a aac -shortest ebb.mp4
```

The frames are BT.601. A `.y4m` cannot say so, and on a picture 1280 or
more wide or over 576 tall mpv guesses BT.709 and shows the colors shifted; the `-colorspace` flags above
keep an encode right at any size.

With `--video`, `--from` and `--length` are rounded to whole frames, so
the two files start and end together, to the sample where the frame rate
divides 44100 (25, 30, 50 and 60 do; 24 does not, and 29.97 is not a frame
rate here).

## Writing a video patch

A video patch is a `.dsp` like any other. What is different:

- **`channels = 3`**: `out0`, `out1` and `out2` are red, green and blue,
  0 black to 1 full. Past 1 is white and under 0 is black, so the
  negative half of an oscillator is black. A patch with fewer outputs
  draws in red only, and genwav says so.
- **`play`** must be wired, to an envelope's: a voice with none draws
  nothing.
- **Voices add like light.** Each note is a voice, every voice draws onto
  the same raster, and a chord is its notes' pictures summed. There is no
  limiter: what adds past white clips there.
- **Where a sample lands** comes from `misc::scan`:

| output | |
|---|---|
| `x`, `y` | 0 to 1, across from the left and down from the top |
| `hsync`, `vsync` | 1 on a line's and a frame's first pixel |
| `frame` | which frame, from 0 |
| `t` | seconds since the start |
| `width`, `size` | a line's pixels and a frame's: what a delay a frame long is set by |

Two dimensions are an expression away: a ring is a `math::sin` of
`(x - 0.5)^2 + (y - 0.5)^2`, a diagonal a sine of `x + y`. On a 4:3
picture multiply the x term by 1.78 for round rings.

Pitch maps straight to the picture. At 60 frames a second a note at f Hz
draws f / 60 bars down the screen, moving by what is left over each frame:
C4 is 4.36 bars, jumping a third of a bar a frame, which strobes. A pitch
near a multiple of 60 Hz holds still or crawls. At a frequency near a
multiple of the line rate (60 x 120 lines, 7200 Hz) the bars stand up and
lean into diagonals.
Fast full-screen stripes are hard to watch; rings and moire are kinder.

**Audio patches do not port.** At a pixel clock of a megahertz or more:

- a time given as samples (`env::adsr`'s units) runs as many times
  faster as the pixel clock is than 44.1 kHz: 26 at 160x120@60, 104 at
  320x240@60; give times in
  `ms` or `s`, which are converted at the synth's own rate;
- the slowest frequency a node can hold is the rate / 2^24, 0.07 Hz at
  160x120@60 and 0.27 Hz at 320x240@60;
- `misc::scan`'s `t` is a float and coarsens as it grows: steps of
  1/4096 s from about half an hour in.

## What the music says

After each audio window the video synth's channels get these chanargs,
where its `.dsp` uses them. Declare one (`@in_level = 0;`) and read it like
any other:

| chanarg | |
|---|---|
| `@in_level` | the mix's loudness, RMS |
| `@in_level0` ... | each audio channel's own, before the master |
| `@in_bass`, `@in_mid`, `@in_high` | the mix's amplitude under 250 Hz, to 2 kHz, and above, DC taken out: a full-scale sine reads 1 |
| `@knob_NAME` | each of the piece's knobs |

Nothing is smoothed. A patch that wants smoothing filters the chanarg, and
one that wants it louder multiplies it: a typical mix's `@in_level` is
around 0.1 to 0.2.

## Feedback

A delay inside a voice ends with the note. An effect on the picture sees
the whole picture and outlives every note, and a delay one frame long is
the oldest video synth trick there is: the picture fed back into itself.
`video/fx/feedback.dsp` does it with `delay::echo` set by `misc::scan`'s
`size`, a line and a pixel more than a frame, so the trail drifts down and
right as it fades, wrapping at the edges:

```
scripts/genwav ... --video video/rings.dsp --video-fx video/fx/feedback.dsp ...
```

An effect hears the raw signal: what clamps to black on screen is still
negative in there. Fed back, a patch's negative half is a dark trail that
eats whatever it drifts into, so `feedback.dsp` holds what comes in at 0
and up first. An effect of your own wants the same.

## The patches

| | |
|---|---|
| `video/moire.dsp` | fine rings per note, wandering slowly, interfering into moire; brighter as the music is louder |
| `video/rings.dsp` | rings from the middle, as many as the note is high, as bright as it is loud |
| `video/fx/feedback.dsp` | the picture a frame late, drifting and fading |

They live in `video/` and not `dsp/`: they are not instruments, and the
page's menus, the corpus sweeps and the sound hashes are all of `dsp/`.

## How it keeps time

After each audio window, the video synth renders every window of its own
that ends at or before the audio's time, counted in whole samples on both
clocks, so the two never drift. A video window is whole lines and no
longer than an audio window, so the picture trails the sound by less than
one, about 23 ms, and a note reaches it within two. (On a picture so short
that one line outlasts an audio window, a window is that line.) It gets the piece's
seed and tempo as the audio synth does. After the music's tail it renders
on to the end of the frame that holds the last of the audio.
