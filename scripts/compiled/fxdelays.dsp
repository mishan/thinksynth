# compilecheck's: an effect whose delays work every path -- an allpass
# line whose length follows the picture, down to none, at a knob's gain;
# an echo whose ring does too, its loop filtered, driven and boosted; and
# an allpass of a fixed length at a negative gain, read off the io node.
name "Fxdelays";
category "Experiments";

    @g = 0.5;

node ionode {
    channels = 3;
    in0 = 0;
    in1 = 0;
    in2 = 0;
    lean = -0.6;
    out0 = ap->out;
    out1 = ec->out;
    out2 = ap2->out;
};

node a0 math::abs { in = ionode->in0; };
node ap delay::allpass { in = ionode->in0; delay = a0->out * 40; gain = @g; };
node ec delay::echo {
    in = ionode->in1;
    size = 30 + a0->out * 50;
    delay = 25;
    feedback = 0.7;
    dry = 0.3;
    tone = 3000;
    low = 100;
    drive = 2;
    boost = 0.3;
};
node ap2 delay::allpass { in = ionode->in2; delay = 7; gain = ionode->lean; };

io ionode;
