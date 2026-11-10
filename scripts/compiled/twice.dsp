# compilecheck's: a time written twice, which plays as the second; a
# compiled voice bound to another rate folds that one too.
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
};

io ionode;
