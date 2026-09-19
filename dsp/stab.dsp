# Stab -- three saws and a fifth, hit and let go.
#
# The rave stab: a chord's worth of brightness for a quarter of a
# second. Three saws detuned in cents, a fourth an octave and a fifth
# up on the `Fifth' mix so one note already sounds like a chord, a
# filter with a little resonance that opens with the hit, and a decay
# with nothing after it. The composer plays it in chords through
# xform::harmonize; this is one voice of one.

name "Stab";
author "Misha Nasledov";
description "Three detuned saws and a fifth through a snapping filter: the rave stab.";

    @detune = 9;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 40;
    @detune.label = "Detune (cents)";

    @fifth = 0.35;
    @fifth.widget = 1;
    @fifth.min = 0;
    @fifth.max = 1;
    @fifth.label = "Fifth";

    @cutoff = 500;
    @cutoff.widget = 1;
    @cutoff.min = 60;
    @cutoff.max = 8000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 3500;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 12000;
    @depth.label = "Envelope Depth (Hz)";

    @res = 0.45;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @fd = 160 ms;
    @fd.widget = 1;
    @fd.min = 5ms;
    @fd.max = 2000ms;
    @fd.label = "Filter Decay";

    @d = 240 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 3000ms;
    @d.label = "Decay";

    @r = 90 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 2000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node s0 osc::simple { freq = freq->out;                              waveform = 1; };
node s1 osc::simple { freq = freq->out * exp2(@detune / 1200);       waveform = 1; };
node s2 osc::simple { freq = freq->out / exp2(@detune / 1200);       waveform = 1; };
node s3 osc::simple { freq = freq->out * 3;                          waveform = 1; };

node fenv env::adsr {
    a = 0;
    d = @fd;
    s = 0;
    r = 10 ms;
    trigger = ionode->trigger;
};

node filt filt::svf {
    in = (s0->out + s1->out + s2->out) * 0.3 + s3->out * 0.3 * @fifth;
    cutoff = @cutoff + fenv->out * @depth;
    res = @res;
};

node env env::adsr {
    a = 1 ms;
    d = @d;
    s = 0;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = filt->out_low;
    in1 = env->out;
};

io ionode;
