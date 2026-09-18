# Hat -- noise through a high-pass, open or closed by how hard it is hit.
#
# The other hat in this tree, hat0.dsp, is two squares ring-modulated
# through white noise and filtered: a metal sound built the only way it
# could be built before there was a noise source with a color or a
# filter with a high output. This one is the short version -- noise, a
# high-pass, an envelope -- and it is one node fewer than the ring
# modulator was.
#
# Velocity squared picks the decay, between `Closed' and `Open'. That is
# the one thing a hat has to do that a drum does not: the same part plays
# both, and which one it is is in the hit. Squared rather than straight
# because a hat is mostly closed and the open one is the accent, so the
# knee belongs at the top of the range.

name "Hat";
author "Misha Nasledov";
description "Noise through a high-pass, open or closed by velocity.";

    @color = 0;
    @color.widget = 1;
    @color.min = 0;
    @color.max = 2;
    @color.label = "Color";

    @tone = 7000;
    @tone.widget = 1;
    @tone.min = 1000;
    @tone.max = 16000;
    @tone.label = "Edge";

    @res = 0.25;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Ring";

    @a = 0.5 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 100ms;
    @a.label = "Attack";

    @clen = 60 ms;
    @clen.widget = 1;
    @clen.min = 0;
    @clen.max = 2000ms;
    @clen.label = "Closed";

    @olen = 420 ms;
    @olen.widget = 1;
    @olen.min = 0;
    @olen.max = 4000ms;
    @olen.label = "Open";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node noise osc::noise {
    color = @color;
    amp = 0.9;
};

# The edge. filt::svf's high output is the input less its low and band
# halves, so what comes through is exactly what the other two dropped.
node hp filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = @res;
};

# Closed at the bottom of the keyboard velocity, open at the top. This
# was a mixer::fade and a math::mul in every other drum here; it is the
# arithmetic it always was.
node env env::ad {
    a = @a;
    d = @clen + (@olen - @clen) * ionode->velocity * ionode->velocity;
    p = ionode->velocity;
};

node vca mixer::mul {
    in0 = hp->out_high;
    in1 = env->out;
};

io ionode;
