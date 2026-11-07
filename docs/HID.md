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

With no profile of your own, the controller plays this:

```
device index {
    base = "C4";
    a = note 0;
    b = note 4;
    trigger_click = note 7;
    stick_y = pitch { step = octave; range = 1; };
    trigger = velocity { min = 40; max = 127; };
};
```

Notes play on the channel the on-screen keyboard is set to, and the
composer's roll hears them as it hears the keyboard's.

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
| `a = octave 1;` | a button | moves every later note up or down an octave |
| `a = hold mod;` | a button | switches layer `mod` on while held |
| `a = toggle mod;` | a button | switches layer `mod` on and off |
| `trigger = "cutoff" { min = 200; max = 8000; shape = exp; };` | an axis | rides a knob of the patch; with no range, the knob's own. `exp` needs a range above 0, and is linear otherwise |
| `stick_y = pitch { step = octave; range = 2; };` | an axis | moves the base note when a note starts (`step = semitone` too) |
| `trigger = velocity { min = 30; max = 127; };` | an axis | sets how hard buttons play |

A note, or a knob, can name its own `{ channel = 3; }`.

A layer, `layer mod { ... };`, holds bindings that replace the profile's own
while it is on, and may set its own `channel` and `base`. The newest layer on
wins. A layer can't rebind a button that switches layers.

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
