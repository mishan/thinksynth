name "test";

node ionode {
    hurr = inmix->blah;

    out0 = delay->out;
    out1 = delay->out;
    channels = 2;
    play = 1; #input->play;

    cutoff = 80;
    res = 0.05;

    dlen = 60000;
    delay1 = 877;
    delay2 = 1124;
    delay3 = 1343;

    dfeed = 0.05;
    ddry1 = 0.5;
    ddry2 = 0;
    delaymix = 0.3;   # reverb amount
};

node input input::alsa {
};

node inmix mixer::fade {
    in0 = input->out;
    in1 = delay3->out;
    fade = ionode->delaymix;
};

node delay delay::echo {
    in = inmix->out;
    size = ionode->dlen;
    delay = ionode->delay1;
    feedback = ionode->dfeed;
    dry = ionode->ddry1;
};

node filt filt::res2pole {
    in = delay->out;
    cutoff = ionode->cutoff;
    res = ionode->res;
};

node delay2 delay::echo {
    in = filt->out_high;
    size = ionode->dlen;
    delay = ionode->delay2;
    feedback = ionode->dfeed;
    dry = ionode->ddry2;
};

node delay3 delay::echo {
    in = delay2->out;
    size = ionode->dlen;
    delay = ionode->delay3;
    feedback = ionode->dfeed;
    dry = ionode->ddry2;
};

io ionode;