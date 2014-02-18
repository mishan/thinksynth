name "test";

node ionode {
    channels = 2;
    out0 = filt->out;
    out1 = filt->out;
    play = 1;

    cutoff = 0.5;
    res = 0.7;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 1;
    reset = modosc->sync;
};

node modosc osc::simple {
    freq = freq->out;
    waveform = 2;
    mul = 2;    # Octave down
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
    in = ammod->out;
    cutoff = ionode->cutoff;
    res = ionode->res;
};

io ionode;