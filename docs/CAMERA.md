# Playing a webcam

A webcam plays thinksynth as an air harp: the picture is split into nine
zones, three by three, and striking a hand through a zone plays its note, as
loud as the strike was quick. Or as a theremin, which tracks your two hands
(below). On the web page for now.

## Setting it up

Start the synth, then press **Camera** beside **MIDI in** and let the page
use the camera. Nine squares appear beside the button and light as the camera
sees each zone move, and the **Camera** pane opens beside the keys: the
picture, mirrored, with the nine zones over it. Each zone lights as it
moves, it strikes its note once the bar along its foot passes the tick,
and a ring flashes round it as it does, wider the harder. A zone that has
just struck strikes again only once it has gone still, and its bar is grey
until then. A swipe that didn't play shows how near it came, or that it
came too soon, and a lamp or a window lighting a zone with no hand in it
is what is setting it off.

The zones are laid out as you see yourself in a mirror: the square at the top
left is your top left. Stand where your hands reach every zone at arm's
length, with the light in front of you rather than behind.

Camera notes play wherever the keys play, on the same channel, as a MIDI
keyboard's do. Press **Camera** again to let the camera go.

## What it plays

```
device camera {
    base = "C4";
    hit_7 = note 0;  hit_8 = note 2;  hit_9 = note 4;
    hit_4 = note 7;  hit_5 = note 9;  hit_6 = note 12;
    hit_1 = note 14; hit_2 = note 16; hit_3 = note 19;
    strike = velocity { min = 40; max = 127; };
};
```

The bottom row is the lowest, and each row climbs a pentatonic scale from
left to right: C D E, G A C, D E G. A note lasts while its zone keeps moving
and ends when the hand stops or leaves.

The profile is in the language of [the Index controller's](HID.md#your-own-profile),
with the camera's own controls:

| Control | |
|---|---|
| `hit_1` .. `hit_9` | buttons: a zone struck, numbered like a phone's keypad from your top left |
| `moving` | a button: down while anything in the picture moves |
| `strike` | how hard the last zone was struck, 0 to 1 |
| `motion` | how much of the picture is moving |
| `x`, `y` | where the movement is: across from your left, and down from the top |
| `spread` | how spread out it is |
| `bright` | how bright the picture is |
| `zone_1` .. `zone_9` | how much each zone is moving |

## The theremin

Pick **Theremin** beside **Camera**, then press **Camera**. Pinch your left
hand, thumb to forefinger, to play; let go to stop. Raise your right hand
for pitch, up to three octaves above C3, and your left for volume: a hand
at the top of the picture is loud, one at the bottom silent. With **Snap to
a scale** on, pitch steps through a pentatonic scale; off, it slides
freely, as a theremin's does. The **Camera** pane
draws what the tracker sees: each hand's points, a ring for each hand that
fills while you pinch, the steps up the right edge with the one you're on
marked (or where your hand is, unsnapped), and your volume up the left.

It plays any patch, and `theremin.dsp` was made for it: a sine that glides
between the steps, hushed by each note's `aux3`, which the left hand sends
as how far it is below the top. A patch that doesn't read `aux3` plays at
one volume. In a piece, the hands play plain notes, which only a patch
bends.

The hand tracker is downloaded the first time you turn the camera on as a
theremin, about 23 MB, and kept for offline use after that; it runs in your browser, and
no picture leaves it. A build made without the tracker's model says the
theremin isn't in it.

The tracker is Google's MediaPipe hand landmarker, under the Apache
License 2.0, shipped with that license in `mediapipe/LICENSE.txt`. Since
thinksynth is GPL version 2 or later, the site with the tracker in it is
distributed under the terms of GPL version 3.

## Limits

- The page plays the camera's own profile; your own comes with the desktop.
- Notes only: bindings that ride a patch's knobs are for the desktop.
- The page reads frames as fast as the screen draws them, 60 a second on
  most screens, whatever the camera can do.
- If the camera stops sending frames (another app takes it, or the tab is
  hidden), its notes let go, and it plays again when frames come back.
