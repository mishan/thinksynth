name "test";

node ionode {
    out0 = static->out;
    out1 = static->out;
    channels = 2;
    play = 1;
};

node lfo osc::simple {
    freq = 0.5;
    waveform = 0;
};

node map env::map {
    in = lfo->out;
    inmin = th_min;
    inmax = th_max;
    outmin = 0;
    outmax = 100;
};

node static osc::static {
    sample = map->out;
};

io ionode;
