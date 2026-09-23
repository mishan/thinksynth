# Grand -- felt hammers on a piano's unison strings.
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
# THE HAMMER is a real one: `filt::pianostring' strikes its strings with
# a mass on a felt spring, F = K d^2.3 for a compression d, and the blow's
# shape and length come out of the felt, the hammer and the string pushing
# on each other rather than out of a filter. A harder blow squeezes stiffer
# felt, so it is shorter and brighter; the waves it launches come back and
# throw the hammer off; it strikes an eighth of the way along, which
# leaves out every 8th partial. From middle C, fitted to a recorded grand
# (University of Iowa's, pianissimo to fortissimo, B0 to C6):
#
# - the hammer weighs 1.7 strings, doubling every thirteen keys up, where
#   the strings are short and light, and halving every eight down to a
#   seventh of one: bass strings are heavy wound ones and a light hammer
#   leaves them quickly, which is why the bass is bright;
#
# - the felt's K is `Hardness' million at middle C, doubling every five
#   keys up, where the hammers are small and very hard: a treble hammer
#   softer than that stays on its short string longer than one period,
#   cancels its own fundamental, and a soft treble note all but vanishes
#   -- C6 at pianissimo came out 31 dB under mezzo-forte, against the
#   recording's 15;
#
# - the hammer's speed is 2^(5 (velocity - 1)): a real one spans about ten
#   to one from pianissimo to fortissimo, and at that the recorded middle
#   C's 26 dB between the two, and its partials' climb from a fundamental
#   alone to half an octave brighter, both come out of the felt.
#
# THE SOUNDBOARD is heard twice: as a gentle knee at 2.5 kHz on the
# strings, the smooth part of its response at the bridge, and as the
# knock below, its modes rung by the blow.
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
description "Felt hammers on unison stiff strings: a physically modeled grand.";
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

    @hardness = 600;
    @hardness.widget = 1;
    @hardness.min = 100;
    @hardness.max = 3000;
    @hardness.label = "Hardness";

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

node string filt::pianostring {
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
    strike = ionode->trigger;
    velocity = exp2(5 * (ionode->velocity - 1));
    mass = max(0.15, 1.7 * exp2(min(ionode->note - 60, 0) / 8 +
                                max(ionode->note - 60, 0) / 13));
    felt = @hardness * exp2(max(ionode->note - 60, 0) / 5);
    exponent = 2.3;
    position = 0.125;
};

# The board's knee, and the registers' balance against the recording:
# up to 3 dB more from middle C down, and 7 dB less by C6.
node voice filt::svf {
    in = string->out * 2 * exp2(clamp((60 - ionode->note) / 32, 0, 0.5) -
                                clamp((ionode->note - 72) / 10, 0, 2.2));
    cutoff = 2500;
    res = 0;
};

# THE KNOCK: the soundboard's own modes, rung by the blow. A recorded
# grand's first 60 ms has about a fiftieth of its energy between the
# partials -- 17 dB under the tone in the middle, 13 at C6 -- and a note
# without it is clean the way an electric piano is. It is not the hammer
# heard: felt makes no click, and the blow reaches the board through the
# string and the bridge. So what drives these is the hammer's force on
# the string, a pulse a few milliseconds long with almost nothing above a
# few kilohertz, and what rings is the board -- eight modes from 110 Hz to
# 1.15 kHz at a Q of about thirty, a thump and not a click. Heard straight
# from a recording of a board, the same knock had twenty times the
# recording's energy above 2 kHz in its first 10 ms.
#
# It is loudest in the bass, doubling every ten keys down from G3, and
# falls away above it, halving every sixteen keys, where the strings' own
# tone is thin and a little knock is a lot; that lands within 2 dB of the
# recording at A1, A2, C4 and C6. `Knock' scales it.
# The blow, scaled for the register.
node blow mixer::mul {
    in0 = string->force;
    in1 = 1.2 * @knock * exp2(clamp((55 - ionode->note) / 10, 0, 5) -
                              clamp((ionode->note - 55) / 16, 0, 5));
};

node mode0 filt::svf {
    in = blow->out;
    cutoff = 110;
    res = 0.985;
};

node mode1 filt::svf {
    in = blow->out;
    cutoff = 170;
    res = 0.985;
};

node mode2 filt::svf {
    in = blow->out;
    cutoff = 240;
    res = 0.985;
};

node mode3 filt::svf {
    in = blow->out;
    cutoff = 330;
    res = 0.985;
};

node mode4 filt::svf {
    in = blow->out;
    cutoff = 450;
    res = 0.985;
};

node mode5 filt::svf {
    in = blow->out;
    cutoff = 620;
    res = 0.985;
};

node mode6 filt::svf {
    in = blow->out;
    cutoff = 850;
    res = 0.985;
};

node mode7 filt::svf {
    in = blow->out;
    cutoff = 1150;
    res = 0.985;
};

# THE BOARD CANNOT RADIATE THE BASS. A soundboard is small against the
# wavelength of a bass note, and below a couple of hundred hertz it moves
# air back and forth around its own edge rather than pushing it away: a
# recorded A1's fundamental is 23 dB under its strongest partial, and
# A2's 13. Without this every bass note is a round boom under its
# partials.
node radiate filt::svf {
    in = voice->out_low +
         (mode0->out_band + mode1->out_band + mode2->out_band + mode3->out_band +
          mode4->out_band + mode5->out_band + mode6->out_band + mode7->out_band);
    cutoff = 180;
    res = 0;
};

# The piano as the player hears it: bass on the left.
node pan mixer::pan {
    in = radiate->out_high * @level;
    pan = clamp((ionode->note - 64) / 44, -1, 1) * @width;
};

io ionode;
