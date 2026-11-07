# Slicer -- a break cut into sixteenths, one on each key.
#
# osc::sample with `slices': loop_break100.wav, a bar of break at 100 to the
# minute, cut into `Slices' equal parts, and the note picks one -- C3 the
# first sixteenth, C#3 the second, on up -- so a chain playing notes
# rearranges the break: the kick on every beat, the snare doubled, a fill
# that never happened. `Reverse' plays each slice back to front. A slice
# plays at its own speed whatever the tempo, so at 100 it fits the grid
# and faster it overlaps; `Release' cuts each one off when its note does.

name "Slicer";
author "Misha Nasledov";
description "A bar of break cut into slices, each played from its own key.";
category "Drums";

    @slices = 16;
    @slices.widget = 1;
    @slices.min = 1;
    @slices.max = 64;
    @slices.label = "Slices";

    @reverse = 0;
    @reverse.widget = 1;
    @reverse.min = 0;
    @reverse.max = 1;
    @reverse.label = "Reverse";

    @r = 8 ms;
    @r.widget = 1;
    @r.min = 1ms;
    @r.max = 500ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = playing->out;
};

node smp osc::sample {
    file = "loop_break100.wav";
    freq = 261.63;
    root = 261.63;
    slices = @slices;
    slice = ionode->note - 48;
    reverse = @reverse;
    trigger = ionode->trigger;
};

node env env::adsr {
    a = 1 ms;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node out mixer::mul {
    in0 = smp->out;
    in1 = env->out * ionode->velocity;
};

node playing math::min {
    in0 = smp->play;
    in1 = env->play;
};

io ionode;
