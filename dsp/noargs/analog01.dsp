name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = amp_env->play;

    wave = 5;
    pw = 0.25;

    cutmin = 0.08;
    cutmax = 0.8;
    res = 0.8;

    amp_a = 10000;
    amp_d = 5000;
    amp_s = 90%;
    amp_r = 20000;

    filt_a = 50000;
    filt_d = 60000;
    filt_s = 85%;
    filt_r = 20000;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = amp_env->out;
};

node amp_env env::adsr {
    a = ionode->amp_a;
    d = ionode->amp_d;
    s = ionode->amp_s;
    r = ionode->amp_r;
    trigger = ionode->trigger;
};

node filt_env env::adsr {
    a = ionode->filt_a;
    d = ionode->filt_d;
    s = ionode->filt_s;
    r = ionode->filt_r;
    trigger = ionode->trigger;
};

node cutmap env::map {
    in = filt_env->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->cutmin;
    outmax = ionode->cutmax;
};

node filt filt::ink2 {
    in = osc1->out;
    cutoff = cutmap->out;
    res = ionode->res;
};

node filt2 filt::ink {
    in = filt->out;
    cutoff = cutmap->out;
    res = ionode->res;
};

node osc1 osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    pw = ionode->pw;
};

io ionode;
