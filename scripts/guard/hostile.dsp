# hostile.dsp -- values a graph may hand a plugin that no plugin may take
# the process down over: a negative ring size or impulse length, a delay of
# a trillion samples, a pulse count of nothing. Each used to ask the audio
# thread for gigabytes, or loop for as long as it takes to count to 2^31.
#
# Beside the harness rather than in dsp/: it is a fixture, and every corpus
# sweep globs dsp/. dspcheck.guard plays notes through it.

name "Guard: hostile values";
description "Every length and delay out of range; it renders, and quietly.";

node ionode {
    out0 = all->out;
    out1 = all->out;
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

node shortring delay::echo {
    in = osc->out;
    size = -1;
    delay = 1000000000000;
};

node farback delay::echo {
    in = osc->out;
    size = 1000;
    delay = -1000000000000;
};

node sine impulse::sine { len = -1; };
node parabola impulse::parabola { len = -1; };
node blackman impulse::blackman { len = -5; };
node square impulse::square { len = 100; num = 0; };

node fsine delay::fir { in = osc->out; impulse = sine->out; };
node fparabola delay::fir { in = osc->out; impulse = parabola->out; };
node fblackman delay::fir { in = osc->out; impulse = blackman->out; };
node fsquare delay::fir { in = osc->out; impulse = square->out; };

node a mixer::add { in0 = shortring->out; in1 = farback->out; };
node b mixer::add { in0 = fsine->out; in1 = fparabola->out; };
node c mixer::add { in0 = fblackman->out; in1 = fsquare->out; };
node ab mixer::add { in0 = a->out; in1 = b->out; };
node abc mixer::add { in0 = ab->out; in1 = c->out; };

node all mixer::mul {
    in0 = abc->out;
    in1 = env->out;
};

io ionode;
