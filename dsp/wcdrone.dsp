# Folded drone -- two operators a fifth apart, folded, breathing.
#
# Two osc::fmop operators, the second a fifth up and a few cents off so the
# pair beat slowly, summed and pushed through dist::fold. misc::drift moves
# the fold's drive about `Fold' by `Wander', slowly and on a curve, so the
# timbre turns over by itself. Then filt::lpg, opened and closed by an
# env::ad that loops -- a function generator, `Breath' seconds up and as
# many down -- so the drone swells brighter and louder and sinks darker and
# quieter, over and over. An env::adsr carries the note itself, and it is
# that, not the loop, the voice's `play' follows: a looping env::ad never
# ends.

name "Folded drone";
author "Misha Nasledov";
description "Two FM operators a fifth apart, a drifting wavefold, breathing through a looping low-pass gate.";
category "Experiments";

    @fold = 3;
    @fold.widget = 1;
    @fold.min = 0.5;
    @fold.max = 10;
    @fold.label = "Fold";

    @wander = 1.5;
    @wander.widget = 1;
    @wander.min = 0;
    @wander.max = 4;
    @wander.label = "Wander";

    @breath = 2500 ms;
    @breath.widget = 1;
    @breath.min = 200 ms;
    @breath.max = 10 s;
    @breath.label = "Breath";

    @depth = 0.7;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 1;
    @depth.label = "Depth";

    @r = 2 s;
    @r.widget = 1;
    @r.min = 50 ms;
    @r.max = 8 s;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = amp->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node low osc::fmop {
    freq = freq->out;
    feedback = 0.15;
};

node fifth osc::fmop {
    freq = freq->out;
    ratio = 1.5035;     # a fifth, 4 cents wide
    feedback = 0.15;
};

node turn misc::drift {
    rate = 0.08;
    depth = @wander;
    center = @fold;
    seed = ionode->note;
};

node fold dist::fold {
    in = (low->out + fifth->out) * 0.45;
    drive = turn->out;
    symmetry = 0.05;
};

node breath env::ad {
    a = @breath;
    d = @breath;
    loop = 1;
};

node gate filt::lpg {
    in = fold->out;
    control = 1 - @depth + @depth * breath->out;
    open = 40;
    close = 200;
    res = 0.25;
};

node amp env::adsr {
    a = 600 ms;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node out mixer::mul {
    in0 = gate->out;
    in1 = amp->out * ionode->velocity * 0.35;
};

io ionode;
