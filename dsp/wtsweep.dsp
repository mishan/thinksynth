# Partial sweep -- a pluck that opens into a pad, one partial at a time.
#
# osc::wavetable over wt_harmonics.wav, whose frames add a saw's partials
# one by one, from the fundamental alone to sixty-four of them. An
# envelope sweeps the table: a note starts at `From', rises to `To' over
# `Sweep', and settles back by `Settle' -- the brightness of a filter
# envelope, made of the partials themselves rather than of a filter.

name "Partial sweep";
author "Misha Nasledov";
description "A wavetable that adds a saw's partials one by one under an envelope.";
category "Synths";

    @from = 0.05;
    @from.widget = 1;
    @from.min = 0;
    @from.max = 1;
    @from.label = "From";

    @to = 0.8;
    @to.widget = 1;
    @to.min = 0;
    @to.max = 1;
    @to.label = "To";

    @sweep = 40 ms;
    @sweep.widget = 1;
    @sweep.min = 1 ms;
    @sweep.max = 2 s;
    @sweep.label = "Sweep";

    @settle = 0.4;
    @settle.widget = 1;
    @settle.min = 0;
    @settle.max = 1;
    @settle.label = "Settle";

    @d = 600 ms;
    @d.widget = 1;
    @d.min = 10 ms;
    @d.max = 4 s;
    @d.label = "Decay";

    @r = 400 ms;
    @r.widget = 1;
    @r.min = 10 ms;
    @r.max = 4 s;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node swept env::adsr {
    a = @sweep;
    d = @d;
    s = @settle;
    r = @r;
    trigger = ionode->trigger;
};

node voice osc::wavetable {
    file = "wt_harmonics.wav";
    freq = freq->out;
    position = @from + (@to - @from) * swept->out * ionode->velocity;
    reset = ionode->trigger;
};

node env env::adsr {
    a = 2 ms;
    d = @d;
    s = 0.6;
    r = @r;
    trigger = ionode->trigger;
};

node out mixer::mul {
    in0 = voice->out;
    in1 = env->out * ionode->velocity * 0.5;
};

io ionode;
