# Rimshot 808 -- a click into something that rings.
#
# Half a millisecond of nothing in particular, pushed into a filter
# with the resonance most of the way up. That is both the original
# circuit and this graph: the 808's rimshot is a pulse into a bridged-T
# network, and what you hear is not the pulse but the network's answer
# to it. A struck thing, built the way struck things work -- an
# excitation and a resonance -- rather than an oscillator with an
# envelope drawn on it.
#
# `Ring' is the filter's resonance and it is the decay control, which is
# why there is no second knob doing that job. Below about 0.9 the answer
# dies inside the click and what comes out is the click; at 0.99 the
# band is a Q of fifty and rings for a couple of tens of milliseconds,
# which is a woodblock. `Decay' is a gate over the top, for cutting the
# tail where a pattern needs it rather than for shaping it.
#
# A narrow band catches less of a click than a wide one, so the makeup
# gain is `0.3 / (1 - Ring)': without it, turning the ring down makes
# the drum louder.
#
# TWO NODES THIS IS NOT BUILT ON, both of which the description suggests.
# `impulse::sine' allocates a buffer `len' samples long and a reader
# indexes it modulo its length *within the window*, so as an audio
# source it repeats once a window and renders differently at 256 samples
# than at 1024 -- which is fine for the FIR kernel it exists for and
# wrong for anything the ear hears. And `filt::resonator's `freq' is not
# where it resonates: it is an allpass coefficient inside a feedback
# loop, whose pole sits at acos(-a0 (1 + fb) / 2 sqrt(fb)), so `1700'
# with the feedback at 0.96 rings at 4.7 kHz. filt::svf's cutoff is in
# hertz and is exact at every setting, and an env::ad click is a single
# click, over when it says it is and bit-identical at every window
# length.
#
# `Snap' mixes a little noise into the excitation. A real rim shot is
# a stick hitting wood and metal at once, and a resonator rung by a
# clean pulse is all metal.

name "Rimshot 808";
author "Misha Nasledov";
description "A click into a 1.7 kHz resonator: the 808 rimshot.";

    @tone = 1700;
    @tone.widget = 1;
    @tone.min = 400;
    @tone.max = 6000;
    @tone.label = "Tone (Hz)";

    @ring = 0.98;
    @ring.widget = 1;
    @ring.min = 0.8;
    @ring.max = 0.99;
    @ring.label = "Ring";

    @snap = 0.4;
    @snap.widget = 1;
    @snap.min = 0;
    @snap.max = 1;
    @snap.label = "Snap";

    @hit = 0.6 ms;
    @hit.widget = 1;
    @hit.min = 0.1ms;
    @hit.max = 20ms;
    @hit.label = "Strike";

    @decay = 70 ms;
    @decay.widget = 1;
    @decay.min = 10ms;
    @decay.max = 1000ms;
    @decay.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The excitation: a click, with as much of the noise stirred into it as
# `Snap' asks for. Both are gone in under a millisecond -- everything
# after that is the resonator talking to itself.
node hit env::ad {
    a = 0;
    d = @hit;
};

node ring filt::svf {
    in = hit->out * (1 - @snap * 0.5) + noise->out * hit->out * @snap * 0.5;
    cutoff = @tone;
    res = @ring;
};

node env env::ad {
    a = 0;
    d = @decay;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = ring->out_band * 0.3 / (1 - @ring);
    in1 = env->out;
};

io ionode;
