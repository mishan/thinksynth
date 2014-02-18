name "test";

node ionode {
    out0 = filt->out;
    out1 = filt->out;
    channels = 2;
    play = input->play;

    cutmin = 80;
    cutmax = 4000;
    res = 4;

    dlen = 20000;
    delaymin = 000;
    delaymax = 2000; # must correlate this to input frequency!  gargh!

    dfeed = 0;
    ddry = 0.6;
};

node input input::alsa {
};

node follower env::follower {
    in = input->out;
    falloff = 3.2;
};

node filtermap env::map {
    in = follower->out;
    inmin = 0;
    inmax = th_max;
    outmin = ionode->cutmin;
    outmax = ionode->cutmax;
};

node delaymap env::map {
    in = input->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->delaymin;
    outmax = ionode->delaymax;
};

node delay delay::echo {
    in = input->out;
    size = ionode->dlen;
    delay = delaymap->out;
    feedback = ionode->dfeed;
    dry = ionode->ddry;
};

node filt filt::res2pole {
    in = delay->out;
    cutoff = filtermap->out;
    res = ionode->res;
};

io ionode;