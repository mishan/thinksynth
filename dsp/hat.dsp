# Hat -- noise through a high-pass, open or closed by how hard it is hit.
#
# The other hat in this tree, hat0.dsp, is two squares ring-modulated
# through white noise and filtered: a metal sound built the only way it
# could be built before there was a noise source with a color or a
# filter with a high output. This one is the short version -- noise, a
# high-pass, an envelope -- and it is one node fewer than the ring
# modulator was.
#
# Velocity squared picks the decay, between `Closed' and `Open'. That is
# the one thing a hat has to do that a drum does not: the same part plays
# both, and which one it is is in the hit. Squared rather than straight
# because a hat is mostly closed and the open one is the accent, so the
# knee belongs at the top of the range.
#
# THE PEDAL. `choke = 1' makes that split a playable pair: an open hat on
# the `and' is cut by the closed one that lands on the beat, because on a
# kit they are one instrument and a foot cannot be in two places. Which
# voice to end is the channel's knowledge and not a graph's, so the
# engine does the ending and the graph only says how fast -- `Pedal
# Close', a few milliseconds of hand on the cymbal.

name "Hat";
author "Misha Nasledov";
description "Noise through a high-pass, open or closed by velocity.";

    @color = 0;
    @color.widget = 1;
    @color.min = 0;
    @color.max = 2;
    @color.label = "Color";

    @tone = 7000;
    @tone.widget = 1;
    @tone.min = 1000;
    @tone.max = 16000;
    @tone.label = "Edge";

    @res = 0.25;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Ring";

    @a = 0.5 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 100ms;
    @a.label = "Attack";

    @clen = 60 ms;
    @clen.widget = 1;
    @clen.min = 0;
    @clen.max = 2000ms;
    @clen.label = "Closed";

    @olen = 420 ms;
    @olen.widget = 1;
    @olen.min = 0;
    @olen.max = 4000ms;
    @olen.label = "Open";

    @pedal = 12 ms;
    @pedal.widget = 1;
    @pedal.min = 1ms;
    @pedal.max = 4000ms;
    @pedal.label = "Pedal Close";

node ionode {
    channels = 2;

    # One hat sounding and one being cut: `choke' is what makes the pair
    # and `poly' is the room their overlap needs.
    choke = 1;
    poly = 2;

    out0 = vca->out;
    out1 = vca->out;
    play = env->play * foot->out;
};

node noise osc::noise {
    color = @color;
    amp = 0.9;
};

# The edge. filt::svf's high output is the input less its low and band
# halves, so what comes through is exactly what the other two dropped.
node hp filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = @res;
};

# Closed at the bottom of the keyboard velocity, open at the top. This
# was a mixer::fade and a math::mul in every other drum here; it is the
# arithmetic it always was.
node env env::ad {
    a = @a;
    d = @clen + (@olen - @clen) * ionode->velocity * ionode->velocity;
    p = ionode->velocity;
};

# The pedal. `choke = 1' on the io node sends every voice that is still
# sounding into its release when the next hat lands, and marks it by
# taking `trigger' negative -- so this is 1 for a key down, 1 for a key
# up, and 0 only for a voice the choke took. A note-off therefore does
# not shorten a hat, which is what a hat wants: its length is in the
# velocity. `play' is multiplied by it too, so a choked voice retires as
# soon as it is quiet rather than sitting out the rest of its decay.
#
# At the top of its range the pedal closes slowly. It still attenuates
# an overlapping hat, and the channel still limits the pair to two voices.
node foot env::adsr {
    a = 0;
    d = 0;
    s = th_max;
    r = @pedal;
    trigger = clamp(1 + ionode->trigger, 0, 1);
};

node vca mixer::mul {
    in0 = hp->out_high;
    in1 = env->out * foot->out;
};

io ionode;
