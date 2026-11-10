# dspcompile.beats's: a time in beats, which a compiled voice can't follow
# the tempo for, so dspcompile refuses it.
name "Beats";
category "Experiments";

node ionode {
    channels = 1;
    out0 = env->out;
    play = env->play;
};

node env env::ad {
    a = 10 ms;
    d = 1 beats;
};

io ionode;
