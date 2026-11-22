# Bend -- phase distortion, swept by an envelope: a sine that opens into
# a saw, a square or a resonance and closes again, with no filter.
#
# osc::pd reads a cosine through a bent phase. `Bend' is how far it is
# bent with the envelope at rest; the envelope adds `Sweep' on top, so
# a strike opens the wave and lets it settle back over `Sweep Decay' to
# `Sweep Sustain'. Played harder, it opens further. On the resonant waves
# the same sweep moves a resonance from the fundamental up to `Resonance'
# times it: the swept, sync-like sound those waves are for.
#
# Two oscillators, one a side, `Detune' cents apart, so the sound is wide
# and slowly moving rather than one point in the middle.

name "Bend";
author "Misha Nasledov";
description "Phase distortion swept by an envelope: a sine that opens into a saw, square or resonance, with no filter.";
category "Synths";

    @wave = 0;
    @wave.widget = 1;
    @wave.min = 0;
    @wave.max = 7;
    @wave.values = "Saw,Square,Pulse,Double,Saw-pulse,Resonant saw,Resonant triangle,Resonant trapezoid";
    @wave.label = "Wave";

    @bend = 0.1;
    @bend.widget = 1;
    @bend.min = 0;
    @bend.max = 1;
    @bend.label = "Bend";

    @sweep = 0.8;
    @sweep.widget = 1;
    @sweep.min = 0;
    @sweep.max = 1;
    @sweep.label = "Sweep";

    @sd = 600 ms;
    @sd.widget = 1;
    @sd.min = 10ms;
    @sd.max = 5000ms;
    @sd.label = "Sweep Decay";

    @ss = 0.2;
    @ss.widget = 1;
    @ss.min = 0;
    @ss.max = 1;
    @ss.label = "Sweep Sustain";

    @reso = 8;
    @reso.widget = 1;
    @reso.min = 1;
    @reso.max = 16;
    @reso.label = "Resonance";

    @detune = 6;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 30;
    @detune.label = "Detune (cents)";

    @a = 3 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000ms;
    @a.label = "Attack";

    @d = 1200 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 5000ms;
    @d.label = "Decay";

    @s = 0.5;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 300 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
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

# The sweep: to the top on the strike, down to `Sweep Sustain' while held.
node sweep env::adsr {
    a = 0;
    d = @sd;
    s = @ss;
    r = @r;
    trigger = ionode->trigger;
};

node oscl osc::pd {
    freq = freq->out * exp2(@detune / 1200);
    waveform = @wave;
    amount = @bend + sweep->out * @sweep * ionode->velocity;
    reso = @reso;
};

node oscr osc::pd {
    freq = freq->out * exp2(-@detune / 1200);
    waveform = @wave;
    amount = @bend + sweep->out * @sweep * ionode->velocity;
    reso = @reso;
    phase = 0.25;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = ionode->velocity * @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node left mixer::mul {
    in0 = oscl->out;
    in1 = env->out * 0.5;
};

node right mixer::mul {
    in0 = oscr->out;
    in1 = env->out * 0.5;
};

io ionode;
