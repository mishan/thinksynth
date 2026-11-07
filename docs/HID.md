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
| Turn your wrist, as a doorknob | pan, springing back to the middle over a few seconds |
| Turn your wrist with a full fist | bends the notes you are holding, two semitones either way |

The notes play on any patch; the rest are `index.dsp`'s knobs, and do
nothing on a patch without them. Each note's own pressure, `aux0`, opens that note's
filter and lifts its level in `index.dsp`; nothing in the controller's own
profile sends it, but `grip_force = aux 0;` in yours does, as does an MPE
controller's key. How closed a fist has to be depends on
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

    grip_force = "level" { min = 0.6; max = 1; smooth = 0.03; };
    grip_force = "bright" { min = 0.3; max = 0.9; smooth = 0.03; };
    grip_mid = "growl" { smooth = 0.05; };
    grip_low = "air" { max = 0.5; smooth = 0.05; };
    stick_y = "wobble" { min = -1; max = 1; };
    turn = "pan" { min = -1; max = 1; smooth = 0.05; };

    layer fist {
        when { grip_top = 0.85; grip_mid = 0.85; grip_low = 0.85; };
        turn = bend { range = 2; smooth = 0.05; };
    };
};

device index right {
    ignore grip_force;
    ignore grip_mid;
    ignore grip_low;
    ignore stick_y;
};

device index left {
    trigger = strike -12 { at = 0.5; };
    a = note -12;
    b = note -5;
    turn = bend { range = 2; of = both; smooth = 0.05; };
    ignore fist;
};
```

## Two controllers

Plug in a pair and each plays its own hand, which each controller says it
is. One that can't say plays whichever hand is free; if a pair comes out
the wrong way round, **Swap hands** in the Controller pane gives each the
other's, remembered by serial number from then on. Alone, either controller plays the first block of the profile below,
whichever hand it is; as a pair, each plays that with its own hand's block
on top:

| Hand | Plays |
|---|---|
| Right | the notes: the trigger strikes, A and B, the trackpad's degree, tilt's octave, turn for pan, and a fist's bend |
| Left | the shape of them: squeeze for level and brightness, the middle fingers for growl, the ring and little fingers for breath, the stick for vibrato, and turn to bend both hands' notes; and notes of its own an octave down |

A bend or an aux `{ of = both; }` reaches the notes the other hand holds
as well as this hand's own, and where both hands bend a note the bends
add. Both hands play the keyboard's channel unless a block says otherwise,
so the same note from both cuts itself off, as it would from two MIDI
keyboards. The Controller pane shows each hand's bindings under its name.

## Your own profile

Your profile is changes to the controller's own, with a block for either
hand and, if you like, `device index left { ... };` and `device index right
{ ... };` for each: write them to `hid/index`
in thinksynth's configuration directory, normally
`~/.config/thinksynth/hid/index` (the Controller pane's Open file starts
one). A control you bind replaces every binding of it outside layers in the
controller's own profile, a layer replaces the layer of its name, `ignore
NAME;` drops a control's bindings outside layers, or a layer, and `base`,
`channel` and `velocity` are yours if you set them. A control's binding
inside one of the controller's own layers stays until you replace that
layer. Your bindings may hold or toggle the controller's own layers. Everything else stays the controller's own, so it reaches you as
it grows:

```
device index {
    grip_mid = "wobble";          # in place of growl
    ignore stick_y;               # no vibrato on the stick
    layer fist {                  # a fist of your own hand's measure
        when { grip_top = 0.7; grip_mid = 0.7; grip_low = 0.7; };
        roll = "bend" { min = -1; max = 1; smooth = 0.05; };
    };
};
```

It is read when thinksynth starts and when the pane's Reload is pressed; one
that can't be read, doesn't parse, or names a control the controller hasn't
got is reported with its line, and the controller plays its own profile
alone. The pane marks the bindings that are yours.

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
| `stick_y = pitch { step = octave; range = 2; };` | an axis | moves the base note when a note starts (`step = semitone` too, or `scale = minor_pentatonic;` to step by its degrees: `major`, `minor`, `dorian`, `pentatonic`, `minor_pentatonic`, `blues`) |
| `trigger = velocity { min = 30; max = 127; };` | an axis | sets how hard buttons play |
| `stick_x = bend { range = 2; };` | an axis | bends every note it is holding, each on its own, up to that many semitones (either way on a centered axis); a note started while bent starts bent, and a note let go of is bent no further. Works on any patch |
| `grip_force = aux 0 { min = 0; max = 1; };` | an axis | sets `aux0` (0 to 3) of every note it is holding, for a patch that reads `ionode->aux0` |
| `grip_force = loop record { at = 0.5; channel = 2; };` | a button or an axis | starts recording a loop of the notes both hands play, and the next time ends it: from then it plays over and over on that channel (by default the channel of the hand that records it), whichever hand played them, as long as the recording was. Each recording is a loop of its own, on top of the others; one shorter than a twentieth of a second, or with no notes, is dropped. A note the hands and the loops share sounds until the last of them lets it go. The pane says when one is recording and how many play, and its Clear loops button stops them, even with no controller left to |
| `b = loop undo;` | a button or an axis | stops the newest loop, or the one recording |
| `twist = loop clear { at = 0.6; };` | a button or an axis | stops every loop. A loop action on an axis acts as the axis rises past `at`, and again only once it has fallen under `off`, as a strike does |

A note, or a knob, can name its own `{ channel = 3; }`. A note can
`{ latch; }`: one press starts it and the next ends it, for a drone. A knob
can be `{ smooth = 0.1; }`, following its axis over that many seconds
rather than stepping with it.

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

### A theremin

A latched note whose pitch the fingers ride an octave either side and whose
level the squeeze rides, on `dsp/index.dsp`:

```
device index {
    a = note 0 { latch; };
    grip_top = bend { center = 0.5; range = 24; smooth = 0.05; };
    grip_force = "level" { smooth = 0.05; };
    roll = "wobble";
};
```

`hid/profiles` holds two whole instruments for a pair, each to copy to
`hid/index` in the configuration directory: `theremin`, a two-handed one
on `dsp/index.dsp`, and `grab`, after Imogen Heap's gloves, where the
right hand plays a scale by how high it is held and the left squeezes to
record loops of it, which `dsp/grab.dsp` plays on channel 2.

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
| `roll` | how far the buttons face left (+) or right (-), 1 at a quarter turn; it eases back toward 0 past that, and as the front tips toward straight up or down, but never jumps |
| `twist` | how fast it is turning around the front, -1 to 1 |
| `turn` | how far it has turned around the front since it was last still, -1 to 1 over a quarter turn, in any pose, however it is held; it springs back to 0 over a few seconds, as a gyro's turn has to |
