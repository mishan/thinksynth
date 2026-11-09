# thinkbox

Patches played on an Espressif ESP32-S3-BOX: eight of them, dxbell,
grab, index, rhodes, pluck, clav, glass and stylophone, each compiled
(`scripts/dspcompile`, docs/KERNELS.md) to eight voices, rendered half on
each core, through the BOX's speaker at 32 kHz; and the touchscreen and
the IMU, read 250 times a second and played through the BOX's profile
(`THH_BOX_PROFILE`) and the patch's own `controller box` block over it, by
the mapper the desktop's controllers have (`hid/thhMapper`, `hid/thhBox`).

The screen is a strip of keys, C minor pentatonic two octaves left to
right, louder the lower it's touched; two fingers play two notes, and a
finger slid onto another key plays that key. The screen draws them, a
color for each note of the scale and darker toward the soft top, the Cs
with a white foot, and lights the ones under the fingers; along the top,
a square for each patch, the playing one lit. Standing upright facing
you, tipping the top away bends the note up and toward you down; turning
it like a wheel does what the patch's block says, making dxbell brighter
or darker. The red button under the screen makes the way it's held now
level, for a BOX lying on a desk or leaning back in its stand. The side
button steps to the next patch, the notes of the last left to ring out.
Each patch is scaled to peak as loud as the others (`generate.sh`).

When the blocks run near their time, two in a row over 75% of it, the
voice sounding longest, a released one before a held one, fades out over
the next block; and a new note then takes over a sounding voice rather
than a free one.

## Building

With ESP-IDF 6 (`. ~/esp/esp-idf/export.sh`) and thinksynth built in
`build/`:

    esp32/box/generate.sh build     # the voices and profiles, from the patches
    cd esp32/box
    idf.py set-target esp32s3
    idf.py -p /dev/ttyACM0 flash monitor

`generate.sh` runs again whenever a patch changes, and lists the
patches. The monitor's lines say how much of a block the patch playing
takes, and each patch's most and how many blocks ran late. A finger counts as
lifted only after 40 ms of the screen reading none, since the touch
controller reports a held finger in bursts. The monitor prints
what the controls read twice a second.

## Marbles

Held, the side button switches the screen from the keys to marbles and
pegs (`game/thgMarbles`), then to Life, then back to the keys; pressed,
it steps to the next patch, and the monitor's line says which patch and
screen. Balls roll the way the BOX is tipped, and
play what they strike, through the BOX's mapper with the game's controls
(`THG_MARBLES_PROFILE`, and a patch's `controller marbles` block over it):
a peg plays the note of its column, harder louder, and a wall plays
`clave.dsp`, on channel 2. A tap puts a peg, or takes one away; a shake
drops another ball in, up to four. It starts with a few pegs scattered
and one ball.

## Life

The third screen is Conway's Life as a step sequencer (`game/thgLife`,
docs/HID.md): a 16 by 8 board, a playhead sweeping its columns eight a
second, its column lit, and playing the live cells it reaches as a
chord, through the patch's `controller life` block or, with none,
`THG_LIFE_PROFILE`: a row a degree of C minor pentatonic up from C4, in
the keys' colors. Each time the playhead comes round, the board advances
a generation. A finger down on a cell brings it to life or kills it, and
dragging from there paints or erases the cells it crosses. A tap on the
patches' row pauses the board, a pause sign at its right end, so the bar
loops as drawn, the playhead still playing it; another tap lets it go on
again. A shake brings a fifth of the cells to life, for a board that died
out. It starts with a glider and a blinker.
