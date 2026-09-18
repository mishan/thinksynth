# Supersaw -- seven saws a few cents apart, the middle one in tune.
#
# The chord sound of every trance record: not one oscillator but a
# spread of them, each a few cents off the last, so the beating between
# them is a slow shimmer rather than a pitch. Seven here, three above
# and three below the center, each `Detune' cents from its neighbor,
# and the detune is an expression -- exp2 of cents over twelve hundred
# -- so it is the same interval at every pitch, which is what amb01's
# detune in hertz could never be.
#
# The odd saws go left and the even ones right, which is width for free:
# the two sides beat differently, and the center saw is in both.
#
# A filt::svf lowpass with its own envelope opens on the attack and sits
# where `Cutoff' says; the amp envelope is slow on purpose. This is a pad
# and a lead in one, and the difference is the attack.

name "Supersaw";
author "Misha Nasledov";
description "Seven detuned saws, odd ones left and even ones right, through a filter with an envelope.";

    @detune = 12;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 60;
    @detune.label = "Detune (cents)";

    @cutoff = 900;
    @cutoff.widget = 1;
    @cutoff.min = 60;
    @cutoff.max = 12000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 2400;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 12000;
    @depth.label = "Envelope Depth (Hz)";

    @res = 0.2;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @fa = 20 ms;
    @fa.widget = 1;
    @fa.min = 0;
    @fa.max = 5000ms;
    @fa.label = "Filter Attack";
    @fd = 600 ms;
    @fd.widget = 1;
    @fd.min = 0;
    @fd.max = 5000ms;
    @fd.label = "Filter Decay";
    @fs = 0.3;
    @fs.widget = 1;
    @fs.min = 0;
    @fs.max = 1;
    @fs.label = "Filter Sustain";
    @fr = 400 ms;
    @fr.widget = 1;
    @fr.min = 0;
    @fr.max = 5000ms;
    @fr.label = "Filter Release";

    @a = 120 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 5000ms;
    @a.label = "Attack";
    @d = 400 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 5000ms;
    @d.label = "Decay";
    @s = 0.8;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @r = 500 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vcal->out;
    out1 = vcar->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The spread: three steps of `Detune' cents each way, as ratios -- the
# ones below divide by the ratio the ones above multiply by.
node s0 osc::simple { freq = freq->out;                               waveform = 1; };
node s1 osc::simple { freq = freq->out * exp2(@detune / 1200);        waveform = 1; };
node s2 osc::simple { freq = freq->out / exp2(@detune / 1200);        waveform = 1; };
node s3 osc::simple { freq = freq->out * exp2(@detune * 2 / 1200);    waveform = 1; };
node s4 osc::simple { freq = freq->out / exp2(@detune * 2 / 1200);    waveform = 1; };
node s5 osc::simple { freq = freq->out * exp2(@detune * 3 / 1200);    waveform = 1; };
node s6 osc::simple { freq = freq->out / exp2(@detune * 3 / 1200);    waveform = 1; };

node fenv env::adsr {
    a = @fa;
    d = @fd;
    s = @fs;
    r = @fr;
    trigger = ionode->trigger;
};

# One filter a side; the center saw is in both.
node filtl filt::svf {
    in = (s0->out + s1->out + s3->out + s5->out) * 0.25;
    cutoff = @cutoff + fenv->out * @depth;
    res = @res;
};

node filtr filt::svf {
    in = (s0->out + s2->out + s4->out + s6->out) * 0.25;
    cutoff = @cutoff + fenv->out * @depth;
    res = @res;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = ionode->velocity * @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vcal mixer::mul {
    in0 = filtl->out_low;
    in1 = env->out;
};

node vcar mixer::mul {
    in0 = filtr->out_low;
    in1 = env->out;
};

io ionode;
