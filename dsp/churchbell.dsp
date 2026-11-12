# Church bell -- a tuned bell struck by its clapper, ringing on long after
# the key is up.
#
# filt::bell is the bell: a church bell's principal partials, tuned, as a
# founder tunes them -- the hum an octave under the strike note, the minor
# third of the tierce that makes it a church bell and not a chime, and the
# nominal an octave over. Each rings and dies on its own, the high ones
# first, so the strike is a shimmer and the tail is a hum.
#
# THE CLAPPER IS SOFT IRON, not a click: a few milliseconds of noise
# through a lowpass. Its lowpass is the brightness: what a strike puts
# into the upper partials is what its spectrum reaches.
#
# THREE BELLS, A HAIR APART. A cast bell is never quite round, so each of
# its partials is a pair of modes a fraction of a hertz apart, and the pair
# beats: the slow pulse in the tail. Here that is the bell beside two more
# 0.12% sharp and flat, one to each side, which also makes it wide.
#
# IN A TOWER. A church bell is heard through stone and air, and a dry one
# sounds like a chime on a table. A patch cannot choose its channel's
# effect, so the space is in the voice: a large, dark delay::fdn. It is
# most of what a voice costs, about 1% of a core.
#
# THE KEY DOES NOT STOP IT. The voice ends when the bell's own `play' says
# the ring has gone: a bell keeps ringing when the rope is let go. The
# tower's tail is three quarters of `Decay', and the hum rings for 1.6
# times it, so by then both are under -80 dBFS.
#
# `Size' takes the bell down to an octave: a bigger bell is a lower one.

name "Church Bell";
author "Misha Nasledov";
description "A tuned church bell, struck: the hum outlasts the shimmer.";
category "Keys";

    # Seconds, as a bare number: a unit in a .dsp is folded into samples,
    # and filt::bell's decay wants seconds.
    @decay = 8;
    @decay.widget = 1;
    @decay.min = 1;
    @decay.max = 30;
    @decay.label = "Decay (s)";

    @bright = 2500;
    @bright.widget = 1;
    @bright.min = 400;
    @bright.max = 9000;
    @bright.label = "Brightness (Hz)";

    @size = 0;
    @size.widget = 1;
    @size.min = 0;
    @size.max = 1;
    @size.label = "Size";

node ionode {
    channels = 2;

    # Each note rings for many seconds.
    poly = 8;

    out0 = (bell->out + bellL->out) * 0.12 + tower->out * 0.10;
    out1 = (bell->out + bellR->out) * 0.12 + tower->out2 * 0.10;
    play = bell->play;
};

node freq misc::midi2freq { note = ionode->note; };

node noise osc::noise {
    color = 0;
    amp = 1;
};

node clap env::ad {
    a = 0.5 ms;
    d = 4 ms;
    p = ionode->velocity;
};

node strike filt::svf {
    in = noise->out * clap->out;
    cutoff = @bright;
    res = 0;
};

node bell filt::bell {
    in = strike->out_low;
    freq = freq->out * exp2(-@size);
    decay = @decay;
};

node bellL filt::bell {
    in = strike->out_low;
    freq = freq->out * exp2(-@size) * 1.0012;
    decay = @decay;
};

node bellR filt::bell {
    in = strike->out_low;
    freq = freq->out * exp2(-@size) * 0.9988;
    decay = @decay;
};

node tower delay::fdn {
    in = (bell->out + bellL->out + bellR->out) * 0.33;
    size = 2.5;
    decay = @decay * 0.75;
    damping = 3000;
    mod = 8;
    rate = 0.3;
    diffuse = 0.7;
};

io ionode;
