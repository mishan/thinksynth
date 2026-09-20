# Filter -- one filter on a channel, for a hand on a knob.
#
# The sweep that is not part of any instrument: the whole mix, or one
# channel of it, taken up or down over eight bars. Every synth in this
# tree has a filter of its own with an envelope behind it, and that is
# the wrong tool for this -- a filter envelope belongs to a note, and a
# DJ's hand belongs to the bar. So: `filt::svf' across a channel's sum
# with nothing driving it but a chanarg, which a `gen::walk', a
# `gen::pump' or a `gen::steps' on the composer side can take anywhere.
#
# `Mode' is a morph and not a switch. At 0 it is the low output, at 1
# the band, at 2 the high, and in between it is a crossfade of the two
# either side -- the three outputs of a state-variable filter sum to its
# input, so every setting between them is a real filter rather than a
# blend of two answers. A switch would also be one line longer, since
# the expression language has no comparison in it.
#
# The resonance is where a sweep gets its drama and where a mix gets
# lost: at 0.9 and above, a cutoff moving through a bass line whistles
# on every note. That is the sound, and it is why the knob goes there.

name "Filter";
author "Misha Nasledov";
description "A state-variable filter across a channel, swept from the composer side.";

    @cutoff = 1200;
    @cutoff.widget = 1;
    @cutoff.min = 40;
    @cutoff.max = 18000;
    @cutoff.label = "Cutoff (Hz)";

    @res = 0.55;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Resonance";

    @mode = 0;
    @mode.widget = 1;
    @mode.min = 0;
    @mode.max = 2;
    @mode.label = "Mode: low, band, high";

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

node filtl filt::svf {
    in = ionode->in0;
    cutoff = @cutoff;
    res = @res;
};

node filtr filt::svf {
    in = ionode->in1;
    cutoff = @cutoff;
    res = @res;
};

# How much of each output, from one knob: 1, 0, 0 at the bottom, 0, 1, 0
# in the middle and 0, 0, 1 at the top, and a straight crossfade between
# them. `abs' is what makes the band a triangle rather than a step.
node lowmix math::clamp {
    in = 1 - @mode;
    lo = 0;
    hi = 1;
};

node bandmix math::clamp {
    in = 1 - abs(@mode - 1);
    lo = 0;
    hi = 1;
};

node highmix math::clamp {
    in = @mode - 1;
    lo = 0;
    hi = 1;
};

node wetl math::add {
    in0 = filtl->out_low * lowmix->out + filtl->out_band * bandmix->out;
    in1 = filtl->out_high * highmix->out;
};

node wetr math::add {
    in0 = filtr->out_low * lowmix->out + filtr->out_band * bandmix->out;
    in1 = filtr->out_high * highmix->out;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = wetl->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = wetr->out;
    fade = @mix;
};

io ionode;
