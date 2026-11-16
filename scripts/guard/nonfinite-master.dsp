# nonfinite-master.dsp -- an effect that is 0/0 on every sample, as the
# master effect of scripts/guard/nonfinite.gen, so that thSynth::process
# has to send the mix out dry and say so.

name "Guard: non-finite master";
description "Every sample of the mix times 0/0.";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = left->out;
    out1 = right->out;
};

node nan math::div {
    in0 = 0;
    in1 = 0;
};

node left mixer::mul {
    in0 = ionode->in0;
    in1 = nan->out;
};

node right mixer::mul {
    in0 = ionode->in1;
    in1 = nan->out;
};

io ionode;
