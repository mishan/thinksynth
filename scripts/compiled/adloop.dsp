# A looping env::ad, which the voice compiler refuses: thkAd's tick, what
# a compiled voice runs, does not loop (scripts/CMakeLists.txt checks the
# refusal).

name "Looping AD";

node ionode {
    out0 = env->out;
    play = env->play;
};

node env env::ad {
    a = 100;
    d = 300;
    loop = 1;
};

io ionode;
