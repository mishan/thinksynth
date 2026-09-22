# Shimmer -- a long reverb whose tail climbs an interval each time round.
#
# fx/space.dsp with delay::fdn's shimmer turned up: a share of the tail
# goes back into the network shifted by `Interval', so every pass of the
# reverb carries a copy an octave up, and a copy of that another octave
# up, each quieter and darker than the last. It is the pad Eno and
# Lanois made from a pitch shifter in a reverb's feedback, and the reason
# the shift is inside the node rather than wired here is that a graph
# may not hold a cycle -- see the head of plugins/delay/fdn.cpp.
#
# `Shimmer' is the share of the tail's power that comes back shifted,
# so the loop's gain is its square and the climb always dies away; near
# the top it takes most of a minute to. `Interval' is a ratio: 2 is the
# octave, 1.5 a fifth up, 0.75 a fourth down. `Damping' is the network's
# low-pass and the shimmer's too, so it is what sets how many octaves up
# the climb is still heard; the shimmer's own path also cuts below 80 Hz,
# because a shift cannot move DC.
#
# Everything else is fx/space.dsp's: the pre-delay, the two uncorrelated
# taps, the cuts on the wet, and the trim that holds the wet at the dry's
# level whatever the decay -- see the head of that file for its
# arithmetic. The shimmer adds to the tail over what the trim expects,
# up to 1 / (1 - shimmer^2) in power: a decibel at the default half and
# seven at the ceiling, which `Mix' is for.
#
# This is an effect graph -- `in0' on the io node. See fx/echo.dsp.

name "Shimmer";
author "Misha Nasledov";
description "A long reverb with a pitch shift in its feedback: the tail climbs an octave each time round.";
category "Effects";

    @predelay = 20 ms;
    @predelay.widget = 1;
    @predelay.min = 0;
    @predelay.max = 500ms;
    @predelay.label = "Pre-delay";

    @decay = 8;
    @decay.widget = 1;
    @decay.min = 0.3;
    @decay.max = 60;
    @decay.label = "Decay (s)";

    @size = 1.5;
    @size.widget = 1;
    @size.min = 0.5;
    @size.max = 3;
    @size.label = "Size";

    @damping = 7000;
    @damping.widget = 1;
    @damping.min = 1000;
    @damping.max = 18000;
    @damping.label = "Damping (Hz)";

    # How far each line's read swings, in samples: what keeps a long
    # tail from settling into a chord. At zero the network is still, and
    # a still network at a long decay is where metallic comes back.
    @motion = 12;
    @motion.widget = 1;
    @motion.min = 0;
    @motion.max = 32;
    @motion.label = "Motion";

    @lowcut = 100;
    @lowcut.widget = 1;
    @lowcut.min = 20;
    @lowcut.max = 1000;
    @lowcut.label = "Low Cut (Hz)";

    @highcut = 12000;
    @highcut.widget = 1;
    @highcut.min = 1000;
    @highcut.max = 18000;
    @highcut.label = "High Cut (Hz)";

    # How much of the tail comes back shifted, as a share of its power.
    @shimmer = 0.5;
    @shimmer.widget = 1;
    @shimmer.min = 0;
    @shimmer.max = 0.9;
    @shimmer.label = "Shimmer";

    @interval = 2;
    @interval.widget = 1;
    @interval.min = 0.5;
    @interval.max = 2;
    @interval.label = "Interval (ratio)";

    @mix = 0.35;
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

node pre delay::echo {
    in = (ionode->in0 + ionode->in1) * 0.5;
    size = 510 ms;
    delay = @predelay + 1;
    feedback = 0;
    dry = 0;
};

node net delay::fdn {
    in = pre->out;
    size = @size;
    decay = @decay;
    damping = @damping;
    mod = @motion;
    rate = 0.3;
    diffuse = 0.7;
    shimmer = @shimmer;
    interval = @interval;
};

node lowl filt::svf {
    in = net->out * pow(1 - pow(10, -0.26 * @size / @decay), 0.5);
    cutoff = @lowcut;
    res = 0;
};

node lowr filt::svf {
    in = net->out2 * pow(1 - pow(10, -0.26 * @size / @decay), 0.5);
    cutoff = @lowcut;
    res = 0;
};

node highl filt::svf {
    in = lowl->out_high;
    cutoff = @highcut;
    res = 0;
};

node highr filt::svf {
    in = lowr->out_high;
    cutoff = @highcut;
    res = 0;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = highl->out_low;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = highr->out_low;
    fade = @mix;
};

io ionode;
