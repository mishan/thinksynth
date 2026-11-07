# Pad -- PADsynth bands under a filter that wanders.
#
# osc::pad renders every partial as a band a few cents wide rather than a
# line, so one oscillator is already a section and needs no detune and no
# chorus to be one. Its `out2' reads the same table from elsewhere, a
# second take of the same pad, and the two go either side of the note's
# place in the field: `Width' spreads them apart from it.
#
# THE FILTER WANDERS. misc::drift moves the cutoff up and down by up to
# `Wander' octaves, slowly and on a curve, each voice its own way (seeded
# by its note), so a held chord breathes without anything being played.
#
# A COMPOSED NOTE PLACES ITSELF. aux0 is where it sits, left to right,
# through mixer::pan; aux1 its brightness, an octave of cutoff either way.
# A note carrying neither is in the middle, as patched -- the convention
# docs/DSP_FORMAT.md gives aux0 and aux1, and what xform::vary and
# xform::cloud write.
#
# The bands are fixed when a voice starts: osc::pad renders a table per
# octave the first time one is asked for, which costs a note-on there.

name "Pad";
author "Misha Nasledov";
description "PADsynth bands through a slowly wandering filter, placed and brightened by each note's aux0 and aux1.";
category "Strings and pads";

    # How wide each partial's band is at the fundamental, in cents.
    @bw = 40;
    @bw.widget = 1;
    @bw.min = 0;
    @bw.max = 120;
    @bw.label = "Bandwidth";

    @tilt = -8;
    @tilt.widget = 1;
    @tilt.min = -18;
    @tilt.max = 0;
    @tilt.label = "Tilt (dB/oct)";

    @cutoff = 2200;
    @cutoff.widget = 1;
    @cutoff.min = 200;
    @cutoff.max = 12000;
    @cutoff.label = "Cutoff (Hz)";

    @res = 0.15;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.8;
    @res.label = "Resonance";

    # Octaves either side of `Cutoff' the drift takes the filter.
    @wander = 0.6;
    @wander.widget = 1;
    @wander.min = 0;
    @wander.max = 2;
    @wander.label = "Wander";

    @rate = 0.12;
    @rate.widget = 1;
    @rate.min = 0.01;
    @rate.max = 2;
    @rate.label = "Wander Rate (Hz)";

    @width = 0.5;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @a = 1500 ms;
    @a.widget = 1;
    @a.min = 5 ms;
    @a.max = 8000 ms;
    @a.label = "Attack";

    @r = 4000 ms;
    @r.widget = 1;
    @r.min = 50 ms;
    @r.max = 15000 ms;
    @r.label = "Release";

    @level = 0.5;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = left->out0 + right->out0;
    out1 = left->out1 + right->out1;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node pad osc::pad {
    freq = freq->out;
    partials = 48;
    bandwidth = @bw;
    bwscale = 1;
    tilt = @tilt;
};

node drift misc::drift {
    rate = @rate;
    depth = @wander;
    seed = ionode->note;
};

node fl filt::svf {
    in = pad->out;
    cutoff = @cutoff * exp2(drift->out + ionode->aux1);
    res = @res;
};

node fr filt::svf {
    in = pad->out2;
    cutoff = @cutoff * exp2(drift->out + ionode->aux1);
    res = @res;
};

node env env::adsr {
    a = @a;
    d = 10 ms;
    s = 0.35 + ionode->velocity * 0.65;
    r = @r;
    p = 0.35 + ionode->velocity * 0.65;
    trigger = ionode->trigger;
};

node left mixer::pan {
    in = fl->out_low * env->out * @level;
    pan = ionode->aux0 - @width;
};

node right mixer::pan {
    in = fr->out_low * env->out * @level;
    pan = ionode->aux0 + @width;
};

io ionode;
