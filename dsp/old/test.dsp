name "test";
description "Test Thinksynth DSP Module";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node env env::adsr {
    a = 2000;
    d = 4000;
    s = 100;
    r = 10000;
    trigger = 0;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node filt filt::rds {
    in = mixer2->out;
    cutoff = 0.4;
    res = map1->out;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 2;
};

node osc2 osc::simple {
    freq = freq->out;
    waveform = 4;
};

node mixer2 mixer::add {
    in0 = osc->out;
    in1 = osc2->out;
};

io ionode;
