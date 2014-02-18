name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;

    fhi = 150;
    flo = 0;
    d = 4000;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node env env::ad {
    a = 0;
    d = ionode->d;
    trigger = 0;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->flo;
    outmax = ionode->fhi;
};

node map2 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node filt filt::rds {
    in = osc->out;
    cutoff = 0.4;
    res = map2->out;
};

node osc osc::simple {
    freq = map1->out;
    waveform = 5;
};

io ionode;
