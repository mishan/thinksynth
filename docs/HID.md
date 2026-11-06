# Playing a Valve Index controller

A wired Valve Index controller plays thinksynth like a keyboard: buttons
play notes, the stick or trackpad moves the pitch, and the trigger, grip
and trackpad drive a patch's knobs. It needs no SteamVR and no base
stations. Linux only, for now.

## Setting it up

Plug the controller in over USB-C. thinksynth finds it on its own, and finds
it again if it is unplugged and plugged back in.

To read it without root, install the udev rule once:

```
hid/install-udev-rule.sh
```

It asks for your password, copies `hid/70-thinksynth-index.rules` into
`/etc/udev/rules.d/`, and applies it to a controller already plugged in. If
thinksynth still can't open it, unplug it and plug it in again.

Without it, thinksynth says on the terminal that it has no permission, and
which file to install.

## What it plays

Load `dsp/index.dsp`, the controller's own instrument, on a channel, and set
the on-screen keyboard to that channel. With no profile of your own:

| Do | Plays |
|---|---|
| Pull the trigger | the note, louder the faster you pull |
| Press A, B | the root, the fifth |
| Slide your thumb up or down the trackpad, then play | up to seven semitones either way |
| Tip the front up or down from where you hold it, then play | an octave up or down |
| Squeeze | louder and brighter |
| Close the middle fingers | growl |
| Close the ring and little fingers | breath |
| Push the stick up or down | vibrato |
| Roll your wrist | pan |
| Roll your wrist with a full fist | bend, two semitones either way |

The notes play on any patch; the rest are `index.dsp`'s knobs, and do
nothing on a patch without them. How closed a fist has to be depends on
the hand: the Controller pane shows each grip zone as you hold it, and
your own profile can move the posture's values.

The profile is:

```
device index {
    base = "C4";

    trigger = strike 0 { at = 0.5; };
    a = note 0;
    b = note 7;
    pad_y = pitch { range = 7; };
    tilt = pitch { step = octave; range = 1; center = 0.25; };

    grip_force = "level" { min = 0.6; max = 1; };
    grip_force = "bright" { min = 0.3; max = 0.9; };
    grip_mid = "growl";
    grip_low = "air" { max = 0.5; };
    stick_y = "wobble" { min = -1; max = 1; };
    roll = "pan" { min = -1; max = 1; };

    layer fist {
        when { grip_top = 0.85; grip_mid = 0.85; grip_low = 0.85; };
        roll = "bend" { min = -1; max = 1; };
    };
};
```

## Your own profile

Write one to `hid/index` in thinksynth's configuration directory, normally
`~/.config/thinksynth/hid/index`. It is read when thinksynth starts; one
that can't be read, doesn't parse, or names a control the controller
hasn't got is reported on the terminal with its line, and the controller
plays its own instead.

A profile is settings and bindings, in the `.gen` file's syntax:

| Setting | Meaning |
|---|---|
| `base = "C4";` | the note a binding's `note 0` plays |
| `channel = 2;` | play here rather than on the keyboard's channel |
| `velocity = 100;` | what buttons play at with no velocity source |

| Binding | On | Does |
|---|---|---|
| `a = note 7;` | a button | plays a note this many semitones above the base |
| `grip_top = strike 7 { at = 0.6; };` | an axis | plays a note as it rises past `at`, louder the faster it got there, and ends it when it falls back under `off` (`at` less 0.2 unless given). `min` and `max` bound its velocity |
| `a = octave 1;` | a button | moves every later note up or down an octave |
| `a = hold mod;` | a button | switches layer `mod` on while held |
| `a = toggle mod;` | a button | switches layer `mod` on and off |
| `trigger = "cutoff" { min = 200; max = 8000; shape = exp; };` | an axis | rides a knob of the patch; with no range, the knob's own. `exp` needs a range above 0, and is linear otherwise |
| `stick_y = pitch { step = octave; range = 2; };` | an axis | moves the base note when a note starts (`step = semitone` too) |
| `trigger = velocity { min = 30; max = 127; };` | an axis | sets how hard buttons play |

A note, or a knob, can name its own `{ channel = 3; }`.

An axis can ride several knobs and pitch axes at once: bind it again, once
for each. Every pitch axis adds its steps, so the trackpad can pick a
degree while tilt picks the octave. A knob or a pitch axis can count from
a `center` other than the axis's own (`tilt = pitch { step = octave;
range = 1; center = 0.25; };` measures from the angle you hold it at).

A layer, `layer mod { ... };`, holds bindings that replace the profile's own
while it is on, and may set its own `channel` and `base`. The newest layer on
wins. A layer can't rebind a button that switches layers.

A layer can also be a posture, on whenever the hand takes a shape:

```
layer fist {
    when { grip_top = 0.7; grip_mid = 0.7; grip_low = 0.7; };
    roll = "bend";
};
```

It is on while every axis it names is at least its value, and goes off
once any of them falls 0.15 under it.

### The controls

Buttons: `a`, `b`, `system`, `trigger_click`, `thumbstick` (pressing it in),
and their touches: `a_touch`, `b_touch`, `system_touch`, `trigger_touch`,
`thumbstick_touch`, `trackpad_touch`.

Axes, 0 to 1 unless they center:

| Axis | |
|---|---|
| `trigger` | how far the trigger is pulled |
| `trigger_near` | the index finger on, or near, the trigger |
| `grip_top`, `grip_mid`, `grip_low` | how close the hand is to each part of the grip: the index and middle fingers at the top, the ring and little fingers together at the bottom |
| `grip_force` | how hard the grip is squeezed |
| `stick_x`, `stick_y` | the stick, -1 to 1, +y away from the hand |
| `pad_x`, `pad_y` | the trackpad where it is touched, -1 to 1, centered when it isn't |
| `pad_force` | how hard the trackpad is pressed |
| `tilt` | the front's angle above level, -1 (straight down) to 1 (straight up); held as you play it, about 0.25 |
| `roll` | turning around the front, -1 to 1 over a quarter turn either way, + as the buttons turn left |
| `twist` | how fast it is turning around the front, -1 to 1 |
