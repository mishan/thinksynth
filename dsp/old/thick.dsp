name "test";

node ionode {
    channels = 2;
    out0 = mixer->out;
    out1 = mixer->out;
    play = env->play;

    cutoff = 0.4;
    res = 0.85;

    dlfo1 = 0.016;
    dlfo2 = 0.083;
    dwave = 3;

    dlen = 1000;

    dlo1 = 150;
    dhi1 = 170;
    dlo2 = 100;
    dlo2 = 120;

    dfeed1 = 0.3;
    dfeed2 = 0.4;
    ddry1 = 0.7;
    ddry2 = 0.6;

    a = 200;
    d = 1000;
    s = 0.8;
    r = 2000;

    wave = 2;
    modwave = 5;
    modmul = 0.5;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = ionode->s;
};

node env env::adsr {
    a = ionode->a;
    d = ionode->d;
    s = suscalc->out;
    r = ionode->r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node osc osc::simple {
    freq = freq->out;
    waveform = ionode->wave;
    reset = modosc->sync;
};

node modosc osc::simple {
    freq = freq->out;
    waveform = ionode->modwave;
    mul = ionode->modmul;
};

node modmap env::map {
    in = modosc->out;
    inmin = th_min;
    inmax = th_max;
    outmin = 1;
    outmax = 0;
};

node ammod math::mul {
    in0 = osc->out;
    in1 = modmap->out;
};

node filt filt::ink {
    in = delay2->out;
    cutoff = ionode->cutoff;
    res = ionode->res;
};

node dlfo1 osc::simple {
    freq = ionode->dlfo1;
    waveform = ionode->dwave;
};

node dlfo2 osc::simple {
    freq = ionode->dlfo2;
    waveform = ionode->dwave;
};

node dmap1 env::map {
    in = dlfo1->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->dlo1;
    outmax = ionode->dhi1;
};

node dmap2 env::map {
    in = dlfo2->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->dlo2;
    outmax = ionode->dhi2;
};

node delay1 delay::echo {
    in = ammod->out;
    size = ionode->dlen;
    delay = dmap1->out;
    feedback = ionode->dfeed1;
    dry = ionode->ddry1;
};

node delay2 delay::echo {
    in = delay1->out;
    size = ionode->dlen;
    delay = dmap2->out;
    feedback = ionode->dfeed2;
    dry = ionode->ddry2;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

io ionode;