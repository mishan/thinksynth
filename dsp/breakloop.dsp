# Break Loop -- a bar of drums that keeps the piece's tempo.
#
# loop_break100.wav is a bar of break played at 100 to the minute on this
# tree's own kit (scripts/loops/break100.gen). osc::stretch plays it with
# its playhead moving at the piece's tempo over 100 -- through
# misc::tempo -- and its grains read at `Pitch', so at 80 or at 140 it is
# the same drums, slower or faster, and at the same pitch. A tape loop
# would be both.
#
# A note starts the loop from the top of the bar and holds it for as
# long as it is down; the note's pitch does nothing, so any note will do.
# `Cutoff' and `Resonance' are a filter on it, which is the other half of
# what a loop is for -- swept open over sixteen bars.

name "Break Loop";
author "Misha Nasledov";
description "A bar of break that follows the piece's tempo at its own pitch, through a resonant filter.";
category "Drums";

    @pitch = 0;
    @pitch.widget = 1;
    @pitch.min = -12;
    @pitch.max = 12;
    @pitch.label = "Pitch (semitones)";

    @grain = 40 ms;
    @grain.widget = 1;
    @grain.min = 10ms;
    @grain.max = 200ms;
    @grain.label = "Grain";

    @cutoff = 16000;
    @cutoff.widget = 1;
    @cutoff.min = 100;
    @cutoff.max = 18000;
    @cutoff.label = "Cutoff (Hz)";

    @res = 0.2;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node tempo misc::tempo { };

node loop osc::stretch {
    file = "loop_break100.wav";
    speed = tempo->bpm / 100;
    pitch = exp2(@pitch / 12);
    size = @grain;
    loop = 1;
    trigger = ionode->trigger;
};

node lp filt::svf {
    in = loop->out;
    cutoff = @cutoff;
    res = @res;
};

node env env::adsr {
    a = 2 ms;
    d = 1 ms;
    s = 1;
    r = 80 ms;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = lp->out_low * @level;
    in1 = env->out;
};

io ionode;
