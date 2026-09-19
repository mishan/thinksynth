# Pluck -- a saw and a square through a filter that snaps shut.
#
# The trance pluck: bright for a few tens of milliseconds and then
# gone. The whole sound is the filter envelope -- fast attack, a short
# decay to almost nothing -- over a saw and a square mixed by `Mix', and
# an amp envelope with almost no sustain under it, so a held note and a
# short one sound the same and the sequencer is what gives it length.

name "Pluck";
author "Misha Nasledov";
description "A saw and a square through a fast filter envelope: the trance pluck.";

    @mix = 0.4;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Square Mix";

    @cutoff = 300;
    @cutoff.widget = 1;
    @cutoff.min = 60;
    @cutoff.max = 8000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 5000;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 14000;
    @depth.label = "Envelope Depth (Hz)";

    @res = 0.35;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @fd = 110 ms;
    @fd.widget = 1;
    @fd.min = 5ms;
    @fd.max = 2000ms;
    @fd.label = "Filter Decay";

    @d = 260 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 3000ms;
    @d.label = "Decay";

    @s = 0.12;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 80 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 2000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node saw osc::simple {
    freq = freq->out;
    waveform = 1;
};

node sqr osc::simple {
    freq = freq->out;
    waveform = 2;
};

# The snap: straight to the top, down over `Filter Decay', nothing held.
node fenv env::adsr {
    a = 0;
    d = @fd;
    s = 0;
    r = 10 ms;
    trigger = ionode->trigger;
};

node filt filt::svf {
    in = saw->out * (1 - @mix) + sqr->out * @mix;
    cutoff = @cutoff + fenv->out * @depth * ionode->velocity;
    res = @res;
};

node env env::adsr {
    a = 1 ms;
    d = @d;
    s = ionode->velocity * @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = filt->out_low;
    in1 = env->out;
};

io ionode;
