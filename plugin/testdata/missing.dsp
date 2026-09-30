# A fixture for controlsgen.missing: it names a wav that is in no sample
# directory, which has to stop the build rather than make a silent plugin.

name "Missing (test)";
author "thinksynth";
description "Two samples, #1 in a folder and #2 in capitals.";
category "Drums";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

    @d = 300 ms;
    @d.widget = 1;
    @d.min = 20ms;
    @d.max = 2000ms;
    @d.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node one osc::sample {
    file = "nowhere.wav";
    freq = 261.63;
    root = 261.63;
    trigger = ionode->trigger;
};

node two osc::sample {
    file = "LOUD.WAV";
    freq = 261.63;
    root = 261.63;
    trigger = ionode->trigger;
};

node env env::ad {
    a = 0;
    d = @d;
    trigger = ionode->trigger;
};

node mix mixer::fade {
    in0 = one->out;
    in1 = two->out;
    fade = @mix;
};

node out mixer::mul {
    in0 = mix->out;
    in1 = env->out;
};

io ionode;
