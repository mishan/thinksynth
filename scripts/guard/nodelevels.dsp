# nodelevels.dsp -- a voice with one node of each kind genwav --node-levels
# tells apart, rendered by scripts/guard/nodelevels.gen:
#
#   `osc' and `vca' sound, at 0.7 a voice (-3.1 dBFS): under full scale
#   in each voice, over it if the two voices' buffers were summed, so a
#   level read across voices would call loud a node no voice drives
#   past full scale;
#   `muted' multiplies by 0, so it is silent in every voice;
#   `lfo' is wired to nothing, so the engine never runs it;
#   `nan' divides 0 by 0, held on the io node where no channel reads it,
#   so the voice plays on and the node is still measured -- read back only
#   by `lfo', which keeps the name in use without running it;
#   `freq' is in Hz, and is given as Hz.

name "Guard: node levels";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = muted->out;
    play = 1;
    held = nan->out;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc osc::simple {
    freq = freq->out;
    amp = 0.7;
};

node vca mixer::mul {
    in0 = osc->out;
    in1 = 1;
};

node muted mixer::mul {
    in0 = osc->out;
    in1 = 0;
};

node nan math::div {
    in0 = 0;
    in1 = 0;
};

node lfo osc::simple {
    freq = 5;
    amp = ionode->held;
};

io ionode;
