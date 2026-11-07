# Playing a webcam

A webcam plays thinksynth as an air harp: the picture is split into nine
zones, three by three, and striking a hand through a zone plays its note, as
loud as the strike was quick. On the web page for now.

## Setting it up

Start the synth, then press **Camera** beside **MIDI in** and let the page
use the camera. Nine squares appear beside the button and light as the camera
sees each zone move.

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

## Limits

- The page plays the camera's own profile; your own comes with the desktop.
- Notes only: bindings that ride a patch's knobs are for the desktop.
- The page reads frames as fast as the screen draws them, 60 a second on
  most screens, whatever the camera can do.
- If the camera stops sending frames (another app takes it, or the tab is
  hidden), its notes let go after a quarter of a second, and it plays again
  when frames come back.
