name "test";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = aenv->play;

    fmin = 20;
    fmax = 240;
    toned = 2500;
    tonemid = 50;
    ampd = 5000;
    ampmid = 70;
    release = 5000;
};

node mixer mixer::mul {
    in0 = osc->out;
    in1 = aenv->out;
};

node tonerelease math::add {  # tone envelope should be longer than amp
    in0 = ionode->release;
    in1 = ionode->ampd;
};

node tenv env::adsr {
    a = 0;
    d = ionode->toned;
    s = ionode->tonemid;
    r = tonerelease->out;
    trigger = 0;
};

node aenv env::adsr {
        a = 0;
        d = ionode->ampd;
        s = ionode->ampmid;
        r = ionode->release;
        trigger = 0;
};

node map1 env::map {
    in = tenv->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->fmin;
    outmax = ionode->fmax;
};

node osc osc::simple {
    freq = map1->out;
    waveform = 5;
};

io ionode;
