# compilecheck's: knobs named for a word C++ has (or) and for set()'s own
# argument (value), which a compiled voice must still set by name.
name "Names";
category "Experiments";

    @value = 0.5;
    @or = 1;

node ionode {
    channels = 1;
    out0 = level->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node op osc::fmop {
    freq = freq->out;
    ratio = 1;
    index = 0;
};

node env env::adsr {
    a = 5 ms;
    d = 50 ms;
    s = 0.5;
    r = 20 ms;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = op->out;
    in1 = env->out;
};

node knobs mixer::mul {
    in0 = @value;
    in1 = @or;
};

node level mixer::mul {
    in0 = vca->out;
    in1 = knobs->out;
};

io ionode;
