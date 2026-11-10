# dspcompile --unit's refusal: a knob on an arg the engine glides.
name "Glide";
category "Experiments";

    @level = 0.5;

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node env env::adsr {
    a = 1 ms;
    d = 1 ms;
    s = 1;
    r = 1 ms;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = env->out;
    in1 = @level;
};

io ionode;
