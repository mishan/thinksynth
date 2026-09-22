# Detune -- two pitch shifters a few cents either side of the dry.
#
# The left side read a few cents sharp and the right a few cents flat,
# each by a delay::pitchshift, mixed against the dry. What the ear takes
# for width is the two sides disagreeing about the pitch, and here they
# disagree by the same amount all the time. That is the difference from
# fx/chorus.dsp, whose taps swing sharp and flat and back with an LFO:
# a chorus's detune comes and goes, so it moves, and a pad through it
# moves with it; this one holds still, which is the studio doubler's
# sound on a vocal and the width every pad wants without the seasick.
#
# `Detune' is in cents each way, so the two sides are twice it apart.
# `Window' is the shifters' own. Their heads wrap |1 - ratio| / window
# times a second, which at eight cents through forty milliseconds is a
# tenth of a hertz -- too slow to hear as a warble -- so what the window
# sets here is how late the wet is, which is half of it. Twenty to fifty
# milliseconds sits behind the dry as a second player, not an echo.
#
# This is an effect graph -- `in0' on the io node. See fx/echo.dsp.

name "Detune";
author "Misha Nasledov";
description "A stereo doubler for a channel: one side a few cents sharp, the other a few cents flat.";
category "Effects";

    @detune = 8;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 30;
    @detune.label = "Detune (cents)";

    @window = 40 ms;
    @window.widget = 1;
    @window.min = 20ms;
    @window.max = 100ms;
    @window.label = "Window";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = up->out;
    out1 = down->out;
};

node up delay::pitchshift {
    in = ionode->in0;
    ratio = exp2(@detune / 1200);
    window = @window;
    mix = @mix;
};

node down delay::pitchshift {
    in = ionode->in1;
    ratio = exp2(-@detune / 1200);
    window = @window;
    mix = @mix;
};

io ionode;
