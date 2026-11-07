# Granular -- a channel heard back as a cloud of its own recent past.
#
# osc::grain reading a live ring rather than a file: the channel's sum is
# written into eight seconds of ring every sample, and grains of `Grain'
# milliseconds, `Density' a second, start `Position' of the way back into
# it and read forward at `Pitch' times its speed. Close to now and short,
# it is a stutter a beat behind the channel; far back and long, it is the
# phrase before this one, smeared.
#
# `Freeze' is the reason it exists. At 1 the ring stops recording and the
# grains go on reading what it last held, for as long as the knob stays
# up -- so a chord played into it and frozen becomes a texture that holds
# while the channel plays on or stops. A composer riding `fx.freeze' is
# a hand on the hold pedal of the whole mix of a channel.
#
# The grains alternate sides, so the wet is stereo whatever came in. The
# channel is summed to one ring, since two rings would be two clouds that
# drifted apart.
#
# This is an effect graph -- `in0' on the io node. See fx/echo.dsp.

name "Granular";
author "Misha Nasledov";
description "A channel's recent past as a grain cloud, with a freeze that holds it.";
category "Effects";

    @position = 0.1;
    @position.widget = 1;
    @position.min = 0;
    @position.max = 1;
    @position.label = "Position";

    @spread = 0.03;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 0.5;
    @spread.label = "Spread";

    @grain = 90 ms;
    @grain.widget = 1;
    @grain.min = 5ms;
    @grain.max = 500ms;
    @grain.label = "Grain";

    @density = 25;
    @density.widget = 1;
    @density.min = 1;
    @density.max = 400;
    @density.label = "Density";

    @pitch = 1;
    @pitch.widget = 1;
    @pitch.min = 0.25;
    @pitch.max = 4;
    @pitch.label = "Pitch (ratio)";

    @jitter = 0.05;
    @jitter.widget = 1;
    @jitter.min = 0;
    @jitter.max = 2;
    @jitter.label = "Jitter (semitones)";

    @freeze = 0;
    @freeze.widget = 1;
    @freeze.min = 0;
    @freeze.max = 1;
    @freeze.label = "Freeze";

    @mix = 0.4;
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

node cloud osc::grain {
    source = 1;
    in = (ionode->in0 + ionode->in1) * 0.5;
    freeze = @freeze;
    position = @position;
    spread = @spread;
    size = @grain;
    density = @density;
    pitch = @pitch;
    jitter = @jitter;
    seed = 1;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = cloud->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = cloud->out2;
    fade = @mix;
};

io ionode;
