# Bed -- three colors of noise, each through a band that wanders.
#
# The floor under an ambient piece: not a note but air, wind, tape hiss,
# the room. White, pink and brown noise each go through a filt::svf band
# pass, and each band's center is walked by its own misc::drift about
# `Low', `Mid' and `High', up to `Wander' octaves either way, a new place
# every thirty seconds or so and a different pace for each, so the three
# never line up and the bed never repeats.
#
# The key moves the whole bed: the three centers are `Low', `Mid' and
# `High' at middle C and scale with the note, so an octave up is the same
# air an octave brighter, and a piece chooses its floor's color by the
# one note it holds for as long as it wants the floor. `Attack' and
# `Release' are long on purpose -- a bed that arrives is a sound; one
# that fades in is a place.

name "Bed";
author "Misha Nasledov";
description "Three colors of noise through slowly wandering bands: the floor under a piece.";
category "Experiments";

    @low = 250;
    @low.widget = 1;
    @low.min = 60;
    @low.max = 1000;
    @low.label = "Low (Hz)";

    @mid = 1200;
    @mid.widget = 1;
    @mid.min = 300;
    @mid.max = 4000;
    @mid.label = "Mid (Hz)";

    @high = 5000;
    @high.widget = 1;
    @high.min = 1500;
    @high.max = 14000;
    @high.label = "High (Hz)";

    @wander = 0.8;
    @wander.widget = 1;
    @wander.min = 0;
    @wander.max = 2;
    @wander.label = "Wander (octaves)";

    @res = 0.6;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";

    @a = 8000 ms;
    @a.widget = 1;
    @a.min = 10ms;
    @a.max = 30000ms;
    @a.label = "Attack";

    @r = 10000 ms;
    @r.widget = 1;
    @r.min = 100ms;
    @r.max = 30000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = left->out;
    out1 = right->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node brown osc::noise { color = 2; };
node pink  osc::noise { color = 1; };
node white osc::noise { color = 0; };

node dl misc::drift { rate = 0.031; depth = @wander; seed = 1; };
node dm misc::drift { rate = 0.043; depth = @wander; seed = 2; };
node dh misc::drift { rate = 0.027; depth = @wander; seed = 3; };

node bl filt::svf { in = brown->out; cutoff = @low * (freq->out / 261.63) * exp2(dl->out);  res = @res; };
node bm filt::svf { in = pink->out;  cutoff = @mid * (freq->out / 261.63) * exp2(dm->out);  res = @res; };
node bh filt::svf { in = white->out; cutoff = @high * (freq->out / 261.63) * exp2(dh->out); res = @res; };

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

# Brown is ten decibels under white at the same peak, so it is given the
# most; the white band is the least, because hiss is what a floor has
# least of before it is a hiss and not a floor. Left and right hear the
# low and high bands leaning opposite ways.
node left mixer::mul {
    in0 = bl->out_band * 1.2 + bm->out_band * 0.6 + bh->out_band * 0.25;
    in1 = env->out * ionode->velocity;
};

node right mixer::mul {
    in0 = bl->out_band * 0.9 + bm->out_band * 0.6 + bh->out_band * 0.35;
    in1 = env->out * ionode->velocity;
};

io ionode;
