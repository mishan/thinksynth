# Grand -- a hammer on one stiff string.
#
# `filt::pianostring' is the string: a waveguide whose partials sit at
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
# - `b', the inharmonicity: about 2e-4 at A0, least in the tenor, 1.6e-2
#   at C8. The wound bass strings are stiffer than their pitch suggests
#   and the short treble ones much stiffer. `Stretch' scales the curve;
#   0 is a harmonic string.
#
# - `decay', the fundamental's T60: 40 s at A0, 8 s at middle C, just
#   over a second at C8, doubling every seventeen keys down. `Sustain'
#   scales it.
#
# - `hidecay', the T60 near 3 kHz. The bridge and the air take the fast
#   motion first, so a bass note's top dies in a second or two while its
#   fundamental rings on. `Tone' is that time; above it the high partials
#   last as long as the low ones and the note is glassy.
#
# THE HAMMER is a pulse, not a burst of noise. Felt on a string stays in
# contact for about 4 ms in the bass and under 1 ms at the top, and a
# harder blow is a shorter one: the pulse's width is the hammer's contact
# time, from `Hammer' at middle C, halving every two octaves up and
# shortening with velocity. Its spectrum is what the string is given, so
# a loud note is brighter because its pulse is narrower -- the same
# mechanism, not a filter pretending to be it. `misc::freq2samples'
# turns the contact time into samples: one cycle of 1/t is t long.
#
# A little noise rides on the pulse for the felt's texture, and a low-pass
# whose corner rises with velocity takes the top off a soft blow.
#
# WHERE IT STRIKES. A hammer an eighth of the way along the string cannot
# excite the partials with a node there: the 8th, 16th, 24th. The pulse
# minus itself an eighth of a period later has exactly those notches.
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
# One string per note, and no soundboard: a real piano's two and three
# unison strings, their beating and their two-stage decay, and the body
# they ring through, are not here yet. What is here is the tuning and
# the decay of one string, done right.

name "Grand";
author "Misha Nasledov";
description "A felt hammer on a stiff string: the piano's stretched partials and its per-key decay.";
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
    @tone = 1.5;
    @tone.widget = 1;
    @tone.min = 0.1;
    @tone.max = 20;
    @tone.label = "Tone (s)";

    # Milliseconds, as a plain number, for the same reason.
    @hammer = 2;
    @hammer.widget = 1;
    @hammer.min = 0.3;
    @hammer.max = 8;
    @hammer.label = "Hammer (ms)";

    @bright = 1500;
    @bright.widget = 1;
    @bright.min = 200;
    @bright.max = 8000;
    @bright.label = "Brightness (Hz)";

    @felt = 0.15;
    @felt.widget = 1;
    @felt.min = 0;
    @felt.max = 1;
    @felt.label = "Felt Noise";

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

# The contact time as a frequency: 1000 / milliseconds.
node contact misc::freq2samples {
    freq = 1000 / (@hammer * exp2((60 - ionode->note) / 24) *
                   (1.5 - ionode->velocity));
};

# A triangle as wide as the contact, as high as the note was played --
# squared, since a hammer's force grows faster than the key's speed.
node pulse env::ad {
    a = contact->out * 0.5;
    d = contact->out * 0.5;
    p = ionode->velocity * ionode->velocity;
};

node noise osc::noise {
    color = 1;
    amp = 1;
};

node felt filt::svf {
    in = pulse->out * (1 + noise->out * @felt);
    cutoff = @bright * exp2(ionode->velocity * 2);
    res = 0;
};

node period misc::freq2samples {
    freq = freq->out;
};

# An eighth of a period, and the pulse minus it.
node strike delay::echo {
    in = felt->out_low;
    size = 4096;
    delay = period->out / 8;
    feedback = 0;
    dry = 0;
};

node string filt::pianostring {
    in = felt->out_low - strike->out;
    freq = freq->out;
    b = 0.0001 * @stretch * (exp2((ionode->note - 48) * 0.1218) +
                             exp2((48 - ionode->note) * 0.0385));
    decay = @sustain * 40 * exp2((21 - ionode->note) / 17);
    hidecay = @tone;
    damper = @damper;
    gate = max(ionode->trigger, clamp((ionode->note - 89.5) * 100, 0, 1));
};

# The piano as the player hears it: bass on the left.
node pan mixer::pan {
    in = string->out * @level;
    pan = clamp((ionode->note - 64) / 44, -1, 1) * @width;
};

io ionode;
