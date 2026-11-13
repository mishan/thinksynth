# Blur -- the channel smeared in time: each frequency's level follows
# slowly, so attacks swell in and what stops fades out where it was.
#
# fft::blur on each side, a frame late. Blur is how long a frequency's
# level takes to follow the channel: short, a softened edge; a second or
# more, a sound that lingers as a wash after it has gone. Random at 0
# lets a fading sound fade as itself; towards 1 its phases turn at
# random as it falls, and the tail shimmers. The sides have seeds of
# their own, so that shimmer is wide. Mix blends the blurred against the
# channel delayed by the same frame, so the two do not comb.
#
# Size, rounded to a power of two, is the frame and the latency: 4096
# blurs a chord's pitches cleanly, 256 keeps the smear close to the
# beat.

name "Blur";
author "Misha Nasledov";
description "Spectral blur: each frequency's level smeared over time, so sounds swell in and fade out.";
category "Effects";

    @blur = 400 ms;
    @blur.widget = 1;
    @blur.min = 0 ms;
    @blur.max = 10000 ms;
    @blur.label = "Blur";

    @random = 0.2;
    @random.widget = 1;
    @random.min = 0;
    @random.max = 1;
    @random.label = "Random";

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

    out0 = bll->out;
    out1 = blr->out;
};

node bll fft::blur {
    in = ionode->in0;
    size = @size;
    blur = @blur;
    random = @random;
    mix = @mix;
    seed = 0;
};

node blr fft::blur {
    in = ionode->in1;
    size = @size;
    blur = @blur;
    random = @random;
    mix = @mix;
    seed = 1;
};

io ionode;
