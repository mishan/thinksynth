# PWM bass -- a pulse whose width breathes, under a resonant low pass.
#
# osc::wavetable over wt_pwm.wav, a band-limited pulse from 50% wide to
# 5%. A slow LFO moves the width by `Depth' around `Width', the PWM bass
# of every analog monosynth, without the aliasing a naive pulse brings at
# the top of its range; a filter envelope opens it on each note.

name "PWM bass";
author "Misha Nasledov";
description "A band-limited pulse wavetable with a breathing width, through a resonant low pass.";
category "Bass";

    @width = 0.4;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @depth = 0.3;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 0.5;
    @depth.label = "Depth";

    @rate = 0.7;
    @rate.widget = 1;
    @rate.min = 0.05;
    @rate.max = 6;
    @rate.label = "Rate (Hz)";

    @cutoff = 300;
    @cutoff.widget = 1;
    @cutoff.min = 60;
    @cutoff.max = 4000;
    @cutoff.label = "Cutoff (Hz)";

    @env = 1800;
    @env.widget = 1;
    @env.min = 0;
    @env.max = 6000;
    @env.label = "Envelope (Hz)";

    @res = 0.35;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @d = 250 ms;
    @d.widget = 1;
    @d.min = 10 ms;
    @d.max = 2 s;
    @d.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node lfo osc::simple {
    freq = @rate;
};

node voice osc::wavetable {
    file = "wt_pwm.wav";
    freq = freq->out;
    position = @width + @depth * lfo->out;
    reset = ionode->trigger;
};

node fenv env::adsr {
    a = 1 ms;
    d = @d;
    s = 0;
    r = 50 ms;
    trigger = ionode->trigger;
};

node filt filt::svf {
    in = voice->out;
    cutoff = @cutoff + @env * fenv->out * ionode->velocity;
    res = @res;
};

node env env::adsr {
    a = 2 ms;
    d = 50 ms;
    s = th_max;
    r = 60 ms;
    trigger = ionode->trigger;
};

node out mixer::mul {
    in0 = filt->out_low;
    in1 = env->out * ionode->velocity * 0.8;
};

io ionode;
