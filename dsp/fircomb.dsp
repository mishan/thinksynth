# FIR Comb -- an oscillator convolved with a row of clicks.
#
# A comb filter is usually a delay line with its output fed back to its
# input, which is a loop and a decay. This is the other kind: a delay
# line with no feedback at all, and several taps read at once. Convolve
# a signal with a train of impulses and every tap adds a copy of it a
# fixed distance back, so the copies reinforce each other at every
# multiple of the spacing and cancel between them. What comes out is a
# fixed set of peaks in the spectrum -- a formant, not a resonance,
# because nothing is ringing.
#
# `impulse::square' builds the row: `Taps' pulses over a response as
# long as `Taps' times one cycle of `Formant', so the spacing is one
# cycle of `Formant' however many taps there are. `delay::fir' does the
# convolving. Neither plugin has another user in the tree, and this is
# the file they were written for.
#
# THE FORMANT DOES NOT FOLLOW THE NOTE, which is the whole reason to
# build a comb this way. The peaks sit where `Formant' puts them, so a
# line played up the keyboard walks its harmonics past a fixed set of
# windows -- loud where a harmonic lands on a peak, thin where it falls
# between. That is a vowel rather than a filter sweep, and it is what a
# resonator does when you play through it rather than with it.
#
# Measured at `Formant' 340, sampling the first two peaks and the two
# troughs between them, over four notes:
#
#     note        340 Hz   680 Hz  |   510 Hz   850 Hz
#     A1         0.00304  0.00144  |  0.00039  0.00036
#     A2         0.00643  0.00229  |  0.00065  0.00069
#     E3         0.01467  0.00272  |  0.00103  0.00067
#     A3         0.00162  0.00436  |  0.00015  0.00098
#
# The peaks stay where they are and the troughs stay between them at
# every pitch. A3 is the one to read twice: its third harmonic is 660
# Hz, which lands on the 680 peak, so that note is loud up there and
# quiet at 340 where nothing of it falls. That is the instrument.
#
# `misc::freq2samples' is what makes that true at any sample rate. The
# spacing is a number of samples and the knob is in hertz, and the
# conversion between them is the device's business.
#
# `Taps' is how sharp the peaks are. Two taps is a gentle ripple across
# the spectrum; eight is narrow and hollow, and the cost of every extra
# one is a longer response and more of the note arriving late.
#
# `Width' is how wide each pulse is as a fraction of the spacing. It is
# a low-pass on the comb itself: narrow pulses reinforce the high peaks
# as strongly as the low ones, wide pulses smear the high ones away.
#
# `Blend' is `delay::fir''s own dry/wet, so the dry oscillator sits
# under the combed one and the effect reads as colour rather than as a
# different instrument.
#
# From `dsp/old/coolbuzz.dsp', 2003, which had six nodes, no envelope
# and `play = 1' -- so its notes never ended.

name "FIR Comb";
author "Misha Nasledov";
description "An oscillator convolved with an impulse train: a fixed formant, not a sweep.";
category "Experiments";

    @formant = 340;
    @formant.widget = 1;
    @formant.min = 60;
    @formant.max = 2000;
    @formant.label = "Formant (Hz)";

    @taps = 5;
    @taps.widget = 1;
    @taps.min = 2;
    @taps.max = 8;
    @taps.step = 1;
    @taps.label = "Taps";

    @width = 0.2;
    @width.widget = 1;
    @width.min = 0.02;
    @width.max = 0.9;
    @width.label = "Width";

    @blend = 0.6;
    @blend.widget = 1;
    @blend.min = 0;
    @blend.max = 1;
    @blend.label = "Blend";

    @wave = 1;
    @wave.widget = 1;
    @wave.min = 0;
    @wave.max = 5;
    @wave.step = 1;
    @wave.values = "Sine,Sawtooth,Square,Triangle,Half-circle,Parabola";
    @wave.label = "Wave";

    @pw = 0.2;
    @pw.widget = 1;
    @pw.min = 0.05;
    @pw.max = 0.95;
    @pw.label = "Pulse Width";

    @cutoff = 0.8;
    @cutoff.widget = 1;
    @cutoff.min = 0.05;
    @cutoff.max = 1;
    @cutoff.label = "Cutoff";

    @res = 0.4;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";

    @a = 6 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @d = 300 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 4000 ms;
    @d.label = "Decay";

    @s = 65%;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 300 ms;
    @r.widget = 1;
    @r.min = 2 ms;
    @r.max = 4000 ms;
    @r.label = "Release";

    @level = 1.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 6;
    @level.label = "Level";

node ionode {
    out0 = vca->out;
    out1 = vca->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# One cycle of the formant, in samples, at whatever rate the device is
# running. Everything about the comb's spacing comes from here.
node spacing misc::freq2samples {
    freq = @formant;
};

node row impulse::square {
    len = spacing->out * @taps;
    num = @taps;
    pw = @width;
};

node osc osc::simple {
    freq = freq->out;
    waveform = @wave;
    pw = @pw;
};

node comb delay::fir {
    in = osc->out;
    impulse = row->out;
    mix = @blend;
};

node filt filt::ink {
    in = comb->out;
    cutoff = @cutoff;
    res = @res;
};

node vca mixer::mul {
    in0 = filt->out * @level;
    in1 = env->out;
};

io ionode;
