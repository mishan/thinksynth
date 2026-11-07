# compilecheck's: an envelope gated by the note, so a release is in what
# the compiled voice must render as the engine does.
name "Gate";
category "Experiments";

    @a = 5 ms;
    @r = 50 ms;

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node op osc::fmop {
    freq = freq->out;
    ratio = 2;
    index = 1.5;
    feedback = 0.3;
};

node env env::adsr {
    a = @a;
    d = 100 ms;
    s = 0.5;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = op->out;
    in1 = env->out * ionode->velocity;
};

io ionode;
