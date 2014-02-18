name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;

#    formant1 = 730;        # male ah
#    formant2 = 1090;
#    formant3 = 2440;

    formant1 = 270;        # male ee
    formant2 = 2290;
    formant3 = 3010;

    wave = 1;
    windowwave = 3;

    subfade1 = 0.3;
    subfade2 = 0.05;

    res = 20;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node mixer mixer::mul {
    in0 = submix1->out;
    in1 = env->out;
};

node env env::adsr {
    a = 20ms;
    d = 60ms;
    s = 50%;
    r = 200ms;
    trigger = ionode->trigger;
};

node window1 osc::window {
    freq = ionode->formant1;
    waveform = ionode->windowwave;
};

node osc11 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window1->sync;
};

node osc12 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window1->sync2;
};

node windowmix1 mixer::fade {
    in0 = osc12->out;
    in1 = osc11->out;
    fade = window1->out;
};

node filt1 filt::res2pole {
    in = windowmix1->out;
    cutoff = ionode->formant1;
    res = ionode->res;
};

node window2 osc::window {
    freq = ionode->formant2;
    waveform = ionode->windowwave;
};

node osc21 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window2->sync;
};

node osc22 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window2->sync2;
};

node windowmix2 mixer::fade {
    in0 = osc22->out;
    in1 = osc21->out;
    fade = window2->out;
};

node filt2 filt::res2pole {
    in = windowmix2->out;
    cutoff = ionode->formant2;
    res = ionode->res;
};

node window3 osc::window {
    freq = ionode->formant3;
    waveform = ionode->windowwave;
};

node osc31 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window3->sync;
};

node osc32 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = window3->sync2;
};

node windowmix3 mixer::fade {
    in0 = osc32->out;
    in1 = osc31->out;
    fade = window3->out;
};

node filt3 filt::res2pole {
    in = windowmix3->out;
    cutoff = ionode->formant3;
    res = ionode->res;
};


node submix1 mixer::fade {
    in0 = filt1->out_band;
    in1 = submix2->out;
    fade = ionode->subfade1;
};

node submix2 mixer::fade {
    in0 = filt2->out_band;
    in1 = filt3->out_band;
    fade = ionode->subfade2;
};

io ionode;
