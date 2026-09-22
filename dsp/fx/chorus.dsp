# Chorus -- a section instead of a player, on a channel's sum.
#
# Two delay::chorus nodes, one a side, whose LFOs sit half a cycle
# apart. That is the whole of the stereo and it is not a trick: what the
# ear takes for width is the two sides disagreeing about the pitch, and
# two taps reading the same line in opposite directions disagree about
# it as much as anything can. One node has one output on purpose -- see
# the head of plugins/delay/chorus.cpp for why taps spread around a whole
# cycle cancel when they are summed to one -- so a graph that wants a
# pair says so, here, where the two sides are visibly two things.
#
# This is an effect graph -- `in0' on the io node -- so it runs on the
# channel's summed voices every window rather than inside a note. A
# chorus does not need the tail an echo needs; what it needs is to be
# after the voices rather than in them, because chorusing every voice
# separately and summing the results is a different and worse sound: the
# copies of a chord are not the chord's copy.
#
# `Mix' is a knob and not a convention: the beating a chorus is made of
# is between the wet and the dry, so at 1 there is nothing left to beat
# against and what comes out is a detuned recording. Half is the usual
# answer. See fx/echo.dsp.

name "Chorus";
author "Misha Nasledov";
description "A stereo chorus for a channel: moving taps on a short delay, half a cycle apart.";
category "Effects";

    @rate = 0.6;
    @rate.widget = 1;
    @rate.min = 0.05;
    @rate.max = 6;
    @rate.label = "Rate (Hz)";

    # How far the taps swing, which is how far out of tune the copies
    # go. The pitch shift is the rate of change of this, so `Rate' and
    # `Depth' both reach it: slow and deep is a tape wobble, fast and
    # shallow is a Leslie.
    @depth = 3 ms;
    @depth.widget = 1;
    @depth.min = 0.1ms;
    @depth.max = 20ms;
    @depth.label = "Depth";

    @delay = 12 ms;
    @delay.widget = 1;
    @delay.min = 2ms;
    @delay.max = 40ms;
    @delay.label = "Delay";

    @voices = 2;
    @voices.widget = 1;
    @voices.min = 1;
    @voices.max = 3;
    @voices.label = "Taps per Side";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    # The engine writes these every window; a file declares them so the
    # nodes below have something to read. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = chl->out;
    out1 = chr->out;
};

node chl delay::chorus {
    in = ionode->in0;
    rate = @rate;
    depth = @depth;
    delay = @delay;
    taps = @voices;
    mix = @mix;
    phase = 0;
};

node chr delay::chorus {
    in = ionode->in1;
    rate = @rate;
    depth = @depth;
    delay = @delay;
    taps = @voices;
    mix = @mix;
    phase = 0.5;
};

io ionode;
