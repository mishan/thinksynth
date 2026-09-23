# Grand -- a piano's unison strings, struck through its soundboard.
#
# `filt::pianostring' is the strings: waveguides whose partials sit at
# n f0 sqrt(1 + b n^2) rather than on the harmonic series, and whose
# fundamental and upper partials each decay in their own time. The
# stretch is what separates a piano from a harpsichord or a dulcimer, and
# it is inside the loop, which is why it is a plugin and this file only
# says which string each key is.
#
# THE STRINGS ARE CURVES OVER THE KEYBOARD. What makes eighty-eight
# strings one instrument is that every property moves smoothly from A0 to
# C8, so each is an expression over `ionode->note':
#
# - `b', the inharmonicity: fitted to the partials of a recorded grand
#   (University of Iowa's), about 1.5e-4 in the bass, least around A1 and
#   C2, 3e-4 at middle C and 1.6e-3 at C6. The wound bass strings are
#   stiffer than their pitch suggests and the short treble ones much
#   stiffer. Too much in the bass is a twang: an earlier curve half again
#   as stiff there turned every low note into a spring. `Stretch' scales
#   the curve; 0 is a harmonic string.
#
# - `decay', the fundamental's T60: 40 s at A0, 15 s at middle C, just
#   over four at C8, doubling every twenty-eight keys down. `Sustain'
#   scales it.
#
# - `hidecay', the T60 near 3 kHz. The bridge and the air take the fast
#   motion first, so the top of a note dies before its fundamental -- but
#   less than it seems: a recorded middle C's 5th to 10th partials fall
#   only 6 to 13 dB a second. `Tone' is that time, 12 s. At a second or
#   two everything above the octave is gone almost at once and what rings
#   on is a fundamental and its octave, which is how an electric piano
#   sustains and not how a piano does.
#
# - `strings': one from A0 to F#1, where each note is a single heavy
#   wound string; two to C3; three above.
#
# - `prompt', how fast the strings moving together lose their energy into
#   the bridge: `Prompt' at middle C, twice as long two octaves down.
#
# THE UNISON IS WHERE THE PIANO IS. The strings of a note are tuned
# `Unison' cents apart and coupled at the bridge. The hammer sets them
# moving together, which the bridge drains in `prompt'; the mistuning
# drifts them out of phase, which the bridge barely feels, and that part
# rings on at the strings' own `decay'. The loud, fast-falling start and
# the long quiet tail of a piano note -- and the slow beating inside it --
# come from that and nothing else. `Unison' is 2.5 cents, where a
# recorded middle C's fundamental wavers as much as this one's; at one it
# decays as smoothly as a tine. Wide, the note is a honky-tonk.
#
# In the bass a cent beats too slowly to matter, and the tail comes from
# the unison not being symmetric: the hammer meets its strings a little
# unevenly and the bridge rocks under strings pulling against each other.
# `Unison Tilt' is how much, and it is what keeps a note ringing even
# with `Unison' at 0.
#
# THE SOUNDBOARD IS THE EXCITATION. Hammer, strings and board are in
# series, and as far as the strings are linear the order does not
# matter, so the board's impulse response can be what the strings are
# struck with rather than a filter after them (Smith and Van Duyne,
# "Commuted piano synthesis", 1995). `piano_board.wav' is that response
# -- a plate's worth of decaying modes, written by scripts/makeboard.py
# -- and `osc::sample' plays it, unpitched, at the top of every note. The
# body of the instrument costs one sample read a voice. It is also where
# the knock at the front of a note comes from: the excitation passes
# through the strings once before they have rung at all.
#
# THE HAMMER is a low-pass on it, and where its corner sits is most of
# what the instrument sounds like. It is fitted to a recorded grand
# (University of Iowa's, mezzo-forte), by the energy in five bands over
# the first 400 ms of B0, A1, C2, A2, C4 and C6: four poles, a corner
# that falls an octave every sixty keys up from 800 Hz at middle C and
# another every eight below A1, where the hammers are big and soft --
# and never below the note itself, where a treble note would only be
# made quieter. `Brightness' is the corner at middle C at mezzo-forte; it
# rises two octaves from pianissimo to fortissimo, since felt stiffens
# the harder it is squeezed. The level is velocity squared, since a
# hammer's force grows faster than the key's speed.
#
# THE DAMPERS are the string's own: `gate' is `ionode->trigger', which
# the engine holds at 2 while the sustain pedal keeps a released key up,
# so the pedal needs nothing here. The top nineteen keys, F#6 up, have no
# damper on a grand and ignore the key coming up. `Damper' is how long a
# damped string takes to fall 60 dB.
#
# `play' IS THE STRING'S. A voice ends when its string is quiet, not
# when its key comes up, so there is no amp envelope at all.
#
# The other strings ringing in sympathy, with the pedal down or not, are
# not here: a voice cannot hear the others.

name "Grand";
author "Misha Nasledov";
description "Unison stiff strings struck through a soundboard: a physically modeled grand.";
category "Keys";

    @stretch = 1;
    @stretch.widget = 1;
    @stretch.min = 0;
    @stretch.max = 4;
    @stretch.label = "Stretch";

    @sustain = 1;
    @sustain.widget = 1;
    @sustain.min = 0.1;
    @sustain.max = 4;
    @sustain.label = "Sustain";

    # Seconds, and deliberately a plain number: a unit in a .dsp is folded
    # into samples, and the string wants seconds.
    @tone = 12;
    @tone.widget = 1;
    @tone.min = 0.1;
    @tone.max = 20;
    @tone.label = "Tone (s)";

    @bright = 800;
    @bright.widget = 1;
    @bright.min = 200;
    @bright.max = 3000;
    @bright.label = "Brightness (Hz)";

    @unison = 2.5;
    @unison.widget = 1;
    @unison.min = 0;
    @unison.max = 20;
    @unison.label = "Unison (cents)";

    # Seconds, as a plain number.
    @prompt = 8;
    @prompt.widget = 1;
    @prompt.min = 0.1;
    @prompt.max = 10;
    @prompt.label = "Prompt (s)";

    @tilt = 0.5;
    @tilt.widget = 1;
    @tilt.min = 0;
    @tilt.max = 1;
    @tilt.label = "Unison Tilt";

    @knock = 1;
    @knock.widget = 1;
    @knock.min = 0;
    @knock.max = 3;
    @knock.label = "Knock";

    @damper = 0.12;
    @damper.widget = 1;
    @damper.min = 0.02;
    @damper.max = 2;
    @damper.label = "Damper (s)";

    @width = 0.6;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    channels = 2;
    poly = 48;
    out0 = pan->out0;
    out1 = pan->out1;
    play = string->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The board, unpitched: `root' and `freq' the same.
node board osc::sample {
    file = "piano_board.wav";
    root = 1;
    freq = 1;
    trigger = ionode->trigger;
};

# The corner, never below the note itself: see THE HAMMER above.
node corner math::max {
    in0 = freq->out;
    in1 = @bright * exp2(((ionode->velocity - 0.63) * 2) +
                         (60 - ionode->note) / 60 -
                         clamp((33 - ionode->note) / 8, 0, 3));
};

# The low-pass is two pairs of poles at a Q of a half, so it takes the
# fundamental down by (1 + (f0 / corner)^2)^2; that much back keeps a dark
# note as loud as a bright one, and the corner decides the timbre and not
# the level. Below middle C they rise again, 7 dB by A1, against what
# the board cannot radiate there. Above
# C5 the notes then ease off, 12 dB by C6, as the recording's do: a treble
# note is nearly all fundamental, and at the bass's level it rings out
# over everything.
node felt filt::svf {
    in = board->out * ionode->velocity * ionode->velocity * 0.9 *
         (1 + (freq->out / corner->out) * (freq->out / corner->out)) *
         (1 + (freq->out / corner->out) * (freq->out / corner->out)) *
         exp2(clamp((60 - ionode->note) / 16, 0, 1.3) -
              clamp((ionode->note - 72) / 6, 0, 2.2));
    cutoff = corner->out;
    res = 0;
};

# Two of them: four poles, 24 dB an octave. At twelve a bass note keeps
# its upper partials 15 to 30 dB louder than a recorded one's, and hears
# them die away over the first second -- a filter sweeping shut, the
# `bowww' of a physical model's bass.
node felt2 filt::svf {
    in = felt->out_low;
    cutoff = corner->out;
    res = 0;
};

node string filt::pianostring {
    in = felt2->out_low;
    freq = freq->out;
    b = 0.000275 * @stretch * exp2((ionode->note - 60) / 9.5) +
        0.000085 * @stretch * exp2((33 - ionode->note) / 15);
    decay = @sustain * 40 * exp2((21 - ionode->note) / 28);
    hidecay = @tone;
    damper = @damper;
    gate = max(ionode->trigger, clamp((ionode->note - 89.5) * 100, 0, 1));
    strings = 1 + (clamp((ionode->note - 30.5) * 100, 0, 1) +
                   clamp((ionode->note - 48.5) * 100, 0, 1));
    unison = @unison;
    prompt = @prompt * exp2((60 - ionode->note) / 24);
    imbalance = @tilt;
};

# THE KNOCK: the board struck by the hammer, heard directly and not through
# the strings. A recorded grand's first 60 ms has about a fiftieth of its
# energy between the partials -- 17 dB under the tone in the middle, 13 at
# C6 -- and a note without it is clean the way an electric piano is. The
# strings cannot supply it: in the treble the hammer's corner is at the
# note, so all that reaches them is the note. It is loudest at the ends,
# the bass's thump and the treble's knock where the tone is thin, and
# least around G3, where the tone covers it: doubling every ten keys
# down from G3 and every hundred up, which lands within 3 dB of the
# recording at A1, A2, C4 and C6. It is darker down the keyboard, 800 Hz
# at middle C and an octave lower every twelve keys down to 500 Hz, with
# the board's lowest modes taken off it: a bass knock is a thump and not
# a boom.
# `Knock' scales it.
node knockhp filt::svf {
    in = board->out;
    cutoff = 150;
    res = 0;
};

node knock filt::svf {
    in = knockhp->out_high * ionode->velocity * ionode->velocity * @knock *
         0.6 *
         exp2(clamp((55 - ionode->note) / 10, 0, 5) +
              clamp((ionode->note - 55) / 100, 0, 5));
    cutoff = clamp(800 * exp2((ionode->note - 60) / 12), 500, 3000);
    res = 0;
};

# THE BOARD CANNOT RADIATE THE BASS. A soundboard is small against the
# wavelength of a bass note, and below a couple of hundred hertz it moves
# air back and forth around its own edge rather than pushing it away: a
# recorded A1's fundamental is 23 dB under its strongest partial, and
# A2's 13. Without this every bass note is a round boom under its
# partials.
node radiate filt::svf {
    in = string->out + knock->out_low;
    cutoff = 180;
    res = 0;
};

# The piano as the player hears it: bass on the left.
node pan mixer::pan {
    in = radiate->out_high * @level;
    pan = clamp((ionode->note - 64) / 44, -1, 1) * @width;
};

io ionode;
