name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = env->play;

    velcalcmin = 1;
    velcalcmax = 14;

    cutmin = 0.5;
    cutmax = 0.2;
    cutlfo = 1;
};

node velcalc env::map {
    in = ionode->velocity;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->velcalcmin;
    outmax = ionode->velcalcmax;
};

node vmul math::mul {
    in0 = velcalc->out;
    in1 = freq->out;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

node env env::adsr {
    a = 0 ms;
    d = 160 ms;
    s = 40%;
    r = 120 ms;
    trigger = ionode->trigger;
};

node map1 env::map {
    in = env->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node map2 env::map {
    in = env->out;
    inmin = th_min;
    inmax = th_max;
    outmin = 0;
    outmax = vmul->out;
};

node cutlfo osc::simple {
    freq = ionode->cutlfo;
};

node cutmap env::map {
    in = cutlfo->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->cutmin;
    outmax = ionode->cutmax;
};

node filt filt::ink2 {
    in = osc->out;
    cutoff = cutmap->out;
    res = map1->out;
};

node osc osc::softsqr {
    freq = freq->out;
    sfreq = map2->out;
    pw = 0.3;
};

io ionode;
