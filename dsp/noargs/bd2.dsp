name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;

    len_d = 5000;
    len_s = 80;
    len_r = 2000;
    freqhi = 0.03;
    freqlo = 0;
    filtq = 0.9;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node env env::adsr {
    a = 0;
    d = ionode->len_d;
    s = ionode->len_s;
    r = ionode->len_r;
    trigger = 0;
};

node freqmap env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->freqlo;
    outmax = ionode->freqhi;
};

node filt filt::res1pole {
    in = osc->out;
    cutoff = freqmap->out;
    res = ionode->filtq;
};

node osc osc::static {
    foo = 0;
};

io ionode;
