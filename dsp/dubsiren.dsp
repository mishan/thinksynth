# Dub Siren -- the box on the engineer's desk with one button.
#
# An oscillator whose pitch is wobbled by an LFO -- a wail at a few
# hertz, a whoop at one, a chirp at twenty -- and swept down from
# `Sweep' semitones above the note at every press, so each hit lands
# like a siren arriving. It is made to go into fx/dub.dsp, where the
# repeats turn one press into a flock of them.

name "Dub Siren";
author "Misha Nasledov";
description "A wobbling oscillator swept down at every press: the siren, for feeding a dub echo.";
category "Leads and stabs";

    @wave = 1;
    @wave.widget = 1;
    @wave.min = 0;
    @wave.max = 2;
    @wave.step = 1;
    @wave.values = "Sawtooth,Pulse,Triangle";
    @wave.label = "Wave";

    @rate = 6;
    @rate.widget = 1;
    @rate.min = 0.2;
    @rate.max = 30;
    @rate.label = "Wobble Rate (Hz)";

    @depth = 5;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 24;
    @depth.label = "Wobble (semitones)";

    @shape = 1;
    @shape.widget = 1;
    @shape.min = 0;
    @shape.max = 3;
    @shape.step = 1;
    @shape.values = "Sine,Saw,Square,Triangle";
    @shape.label = "Wobble Shape";

    @sweep = 12;
    @sweep.widget = 1;
    @sweep.min = -24;
    @sweep.max = 24;
    @sweep.label = "Sweep (semitones)";

    @sweeptime = 400 ms;
    @sweeptime.widget = 1;
    @sweeptime.min = 10ms;
    @sweeptime.max = 4000ms;
    @sweeptime.label = "Sweep Time";

    @cutoff = 3000;
    @cutoff.widget = 1;
    @cutoff.min = 200;
    @cutoff.max = 12000;
    @cutoff.label = "Cutoff (Hz)";

    @r = 150 ms;
    @r.widget = 1;
    @r.min = 70ms;
    @r.max = 2000ms;
    @r.label = "Release";

    @level = 0.6;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note;
};

node lfo osc::simple {
    freq = @rate;
    waveform = @shape;
};

# From `Sweep' above the note to the note, over `Sweep Time'.
node swoop env::ad {
    a = 0;
    d = @sweeptime;
    p = 1;
};

node osc osc::blep {
    freq = pitch->out * exp2((lfo->out * @depth + swoop->out * @sweep) / 12);
    waveform = @wave;
    pw = 0.5;
};

node lp filt::svf {
    in = osc->out;
    cutoff = @cutoff;
    res = 0.3;
};

node env env::adsr {
    a = 3 ms;
    d = 1 ms;
    s = 1;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = lp->out_low * @level;
    in1 = env->out;
};

io ionode;
