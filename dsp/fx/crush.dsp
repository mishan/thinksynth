# Crush -- a channel through an early sampler's converters.
#
# dist::crush on each side: fewer bits, and a sample-and-hold at `Rate'
# with no filter in front of it, so what is above half of `Rate' folds
# back down instead of going away. 12 bits at 26 kHz is an SP-1200; 8 at
# 11 kHz is a cheap one. `Mix' under 1 sets the grit beside the clean
# signal rather than in place of it.

name "Crush";
author "Misha Nasledov";
description "Bit and sample-rate reduction with no anti-alias filter: an early sampler's grit.";
category "Effects";

    @bits = 12;
    @bits.widget = 1;
    @bits.min = 1;
    @bits.max = 16;
    @bits.label = "Bits";

    @rate = 26040;
    @rate.widget = 1;
    @rate.min = 1000;
    @rate.max = 44100;
    @rate.label = "Rate (Hz)";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

node crushl dist::crush {
    in = ionode->in0;
    bits = @bits;
    rate = @rate;
};

node crushr dist::crush {
    in = ionode->in1;
    bits = @bits;
    rate = @rate;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = crushl->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = crushr->out;
    fade = @mix;
};

io ionode;
