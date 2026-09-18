# sound.dsp -- the well-behaved half of the guard's gate. Its job is to make
# an unmistakable noise on whatever channel it is loaded onto, so that "the
# rest of the mix survived" has something to be measured on.
#
# Beside the harness rather than in dsp/: it is a fixture, and every corpus
# sweep globs dsp/.
#
# See scripts/dspsweep.cpp and scripts/guard/nonfinite.dsp.

name "Guard: sound";
description "A sine under an envelope. Nothing here is unusual.";

node ionode {
    out0 = mix->out;
    out1 = mix->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 0;
};

node env env::adsr {
    a = 200;
    d = 4000;
    s = 0.4;
    r = 4000;
    p = 0.8;
    trigger = ionode->trigger;
};

node mix mixer::mul {
    in0 = osc->out;
    in1 = env->out;
};

io ionode;
