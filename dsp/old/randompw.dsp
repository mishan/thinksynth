name "test";

node ionode {
    out0 = filt->out;
    out1 = filt->out;
    channels = 2;
    play = 1;

    cutoff = 0.6;
    res = 0.9;

    waveform = 2;

    pwmin = 0.3;
    pwmax = 0.6;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node rand osc::static {
    stupid_bug = 1;
};

node map1 env::map {
    in = rand->out;
    inmin = th_min;
    inmax = th_max;
    outmin = ionode->pwmin;
    outmax = ionode->pwmax;
};

node latch misc::latch {
    in = map1->out;
    latch = osc->sync;
};

node filt filt::rds {
    in = osc->out;
    cutoff = ionode->cutoff;
    res = ionode->res;
};

node osc osc::simple {
    freq = freq->out;
    waveform = ionode->waveform;
    pw = latch->out;
#    blah = print->foo;
};

node print misc::print {
    in = latch->out;
};

io ionode;
