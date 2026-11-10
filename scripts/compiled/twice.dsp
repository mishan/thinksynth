# compilecheck's: a time written twice, which plays as the second, and a
# value written as a time and then as a percentage, which plays as the
# percentage; a compiled voice bound to another rate folds only times.
name "Twice";
category "Experiments";

node ionode {
    channels = 1;
    out0 = env->out;
    play = env->play;
};

node env env::ad {
    a = 10 ms;
    d = 20 ms;
    d = 5 ms;
    p = 1 ms;
    p = 80%;
};

io ionode;
