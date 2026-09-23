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
# come from that and nothing else. Wide, the note is a honky-tonk.
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
# THE HAMMER is a low-pass on it. Felt stiffens the harder it is
# squeezed, so a harder blow is a shorter one and puts more of the board
# into the strings: the corner rises two octaves from pianissimo to
# fortissimo. It rises an octave every two octaves up the keyboard too,
# where the hammers are smaller and harder. `Brightness' is the corner
# at middle C, played softly. The level is velocity squared, since a
# hammer's force grows faster than the key's speed.
#
# WHERE IT STRIKES. A hammer an eighth of the way along the string cannot
# excite the partials with a node there: the 8th, 16th, 24th. The pulse
# minus itself an eighth of a period later has exactly those notches.
# It also takes out the board's lowest modes under a treble note, which
# the short strings up there cannot take in either.
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
    @tone = 1.5;
    @tone.widget = 1;
    @tone.min = 0.1;
    @tone.max = 20;
    @tone.label = "Tone (s)";

    @bright = 700;
    @bright.widget = 1;
    @bright.min = 100;
    @bright.max = 4000;
    @bright.label = "Brightness (Hz)";

    @unison = 1;
    @unison.widget = 1;
    @unison.min = 0;
    @unison.max = 20;
    @unison.label = "Unison (cents)";

    # Seconds, as a plain number.
    @prompt = 3;
    @prompt.widget = 1;
    @prompt.min = 0.1;
    @prompt.max = 10;
    @prompt.label = "Prompt (s)";

    @tilt = 0.5;
    @tilt.widget = 1;
    @tilt.min = 0;
    @tilt.max = 1;
    @tilt.label = "Unison Tilt";

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

# The board gives the bass less than a pulse did: most of its energy is
# above the lowest strings' fundamentals. Up to 7 dB more from middle C
# down puts the bass back where it balances.
node felt filt::svf {
    in = board->out * ionode->velocity * ionode->velocity * 0.4 *
         (1 + clamp((60 - ionode->note) / 30, 0, 1.5));
    cutoff = @bright * exp2((ionode->velocity * 2) +
                            (ionode->note - 60) / 24);
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
    strings = 1 + (clamp((ionode->note - 30.5) * 100, 0, 1) +
                   clamp((ionode->note - 48.5) * 100, 0, 1));
    unison = @unison;
    prompt = @prompt * exp2((60 - ionode->note) / 24);
    imbalance = @tilt;
};

# The piano as the player hears it: bass on the left.
node pan mixer::pan {
    in = string->out * @level;
    pan = clamp((ionode->note - 64) / 44, -1, 1) * @width;
};

io ionode;
