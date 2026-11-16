# Shift -- the channel's partials moved in frequency: by an interval,
# so it plays in another key, and by a number of hertz, so harmonics
# stop being harmonic and a tone turns to a bell or a ring.
#
# fft::shift on each side, a frame late. Pitch is in semitones, 12 an
# octave up. Offset adds hertz to every partial after that: a few
# hertz beat against the dry, a few hundred make anything metallic. A
# partial pushed below 0 Hz comes back up reflected, as from a
# frequency shifter. Formant at 0 moves the timbre with the pitch, the
# chipmunk a voice has an octave up; at 1 the resonances stay where
# they were and only the notes move. Mix blends the shifted against
# the channel delayed by the same frame, so a fifth's harmony does not
# comb with the dry.
#
# Size, rounded to a power of two, is the frame and the latency: 2048
# moves a bass cleanly, 256 keeps the attacks close at the cost of
# low partials, which under two bins (344 Hz at 256) blur.

name "Shift";
author "Misha Nasledov";
description "Spectral pitch and frequency shift: every partial moved by an interval and an offset in hertz.";
category "Effects";

    @pitch = 7;
    @pitch.widget = 1;
    @pitch.min = -24;
    @pitch.max = 24;
    @pitch.label = "Pitch (semitones)";

    @offset = 0;
    @offset.widget = 1;
    @offset.min = -500;
    @offset.max = 500;
    @offset.label = "Offset (Hz)";

    @formant = 0;
    @formant.widget = 1;
    @formant.min = 0;
    @formant.max = 1;
    @formant.label = "Formant";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

    @size = 2048;
    @size.widget = 1;
    @size.min = 256;
    @size.max = 4096;
    @size.label = "Size (samples)";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = shl->out;
    out1 = shr->out;
};

node shl fft::shift {
    in = ionode->in0;
    size = @size;
    ratio = exp2(@pitch / 12);
    offset = @offset;
    formant = @formant;
    mix = @mix;
};

node shr fft::shift {
    in = ionode->in1;
    size = @size;
    ratio = exp2(@pitch / 12);
    offset = @offset;
    formant = @formant;
    mix = @mix;
};

io ionode;
