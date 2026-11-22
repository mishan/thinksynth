# compilecheck's: osc::pd with its amount on an envelope, so the control is
# made every sample, and hard-synced to a blep at the note, so a restart's
# correction is in what the compiled voice must render as the engine does.
name "Pd";
category "Experiments";

    @wave = 6;
    @reso = 5.3;

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node master osc::blep {
    freq = freq->out;
};

node env env::adsr {
    a = 5 ms;
    d = 200 ms;
    s = 0.3;
    r = 50 ms;
    trigger = ionode->trigger;
};

node osc osc::pd {
    freq = freq->out * 2.7;
    waveform = @wave;
    amount = env->out;
    reso = @reso;
    reset = master->edge;
};

node vca mixer::mul {
    in0 = osc->out;
    in1 = env->out * ionode->velocity;
};

io ionode;
