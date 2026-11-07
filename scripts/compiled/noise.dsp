# compilecheck's: a compiled noise against the engine's, by its level and
# its slope, as their streams are seeded apart.
name "Noise";
category "Experiments";

    @color = 0;

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node noise osc::noise {
    color = @color;
};

node env env::adsr {
    a = 1 ms;
    d = 1 ms;
    s = 1;
    r = 1 ms;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = noise->out;
    in1 = env->out;
};

io ionode;
