# Riser -- noise climbing for as long as the note is held.
#
# White noise through a resonant band-pass whose center climbs from `From'
# to `To' over `Rise', on an octave scale, and a sine under it climbing
# from the note to an octave up the same way, both swelling in as they go:
# the sweep that says the drop is coming. Hold the note for the length of
# the build and set `Rise' to match; let go and it is gone in `Release'.

name "Riser";
author "Misha Nasledov";
description "A noise sweep with a rising tone under it, climbing over a build.";
category "Experiments";

    @rise = 8000 ms;
    @rise.widget = 1;
    @rise.min = 100ms;
    @rise.max = 60000ms;
    @rise.label = "Rise";

    @from = 300;
    @from.widget = 1;
    @from.min = 40;
    @from.max = 4000;
    @from.label = "From (Hz)";

    @to = 9000;
    @to.widget = 1;
    @to.min = 1000;
    @to.max = 18000;
    @to.label = "To (Hz)";

    @tone = 0.3;
    @tone.widget = 1;
    @tone.min = 0;
    @tone.max = 1;
    @tone.label = "Tone";

    @r = 60 ms;
    @r.widget = 1;
    @r.min = 5ms;
    @r.max = 4000ms;
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

node env env::adsr {
    a = @rise;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node hiss osc::noise {
    color = 0;
};

node band filt::svf {
    in = hiss->out;
    cutoff = @from * pow(@to / @from, env->out);
    res = 0.7;
};

node climb osc::simple {
    freq = freq->out * exp2(env->out);
    waveform = 0;
};

node vca mixer::mul {
    in0 = band->out_band * 1.5 + climb->out * @tone;
    in1 = env->out * env->out * ionode->velocity;
};

io ionode;
