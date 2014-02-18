name "test";

node ionode {
    out0 = delay->out;
    out1 = delay2->out;
    channels = 2;
    play = wav->play;

    falloff = 2.7;

    cutmin = 200;
    cutmax = 8000;
    res = 1.2;

    dlen = 44100;
    delaymin = 00;
    delaymax = 600;

    dlfo2 = 0.0502;
    delaymin2 = 600;
    delaymax2 = 00;

    satmin = 0;
    satmax = 8;

    dfeed = 0;
    ddry = 0.7;
};

node wav input::wav {
};

node follower env::follower {
    in = wav->out;
    falloff = ionode->falloff;
};

node satmap env::map {
    in = follower->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->satmin;
    outmax = ionode->satmax;
};

node filtermap env::map {
    in = follower->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->cutmin;
    outmax = ionode->cutmax;
};

node dlfo osc::simple {
    freq = ionode->dlfo;
};

node delaymap env::map {
    in = dlfo->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->delaymin;
    outmax = ionode->delaymax;
};

node delay delay::echo {
    in = filt->out;
    size = ionode->dlen;
    delay = delaymap->out;
    feedback = ionode->dfeed;
    dry = ionode->ddry;
};

node dlfo2 osc::simple {
    freq = ionode->dlfo2;
};

node delaymap2 env::map {
    in = dlfo2->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->delaymin2;
    outmax = ionode->delaymax2;
};

node delay2 delay::echo {
    in = filt->out;
    size = ionode->dlen;
    delay = delaymap2->out;
    feedback = ionode->dfeed;
    dry = ionode->ddry;
};

node filt filt::res2pole {
    in = sat->out;
    cutoff = filtermap->out;
    res = ionode->res;
};

node sat dist::inksat {
    in = wav->out;
    factor = satmap->out;
};

io ionode;