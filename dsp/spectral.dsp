# Spectral -- two ramps of different steepness, subtracted, in a pair of
# tuned combs.
#
# Additive synthesis builds a spectrum by adding partials. This does it
# by taking one away: `osc::sinsaw' and `osc::buzzer' are both shaped
# ramps of the same period, differing only in how their harmonics roll
# off, and subtracting one from the other leaves the difference between
# two harmonic series. Move either shape and the difference moves, so
# the spectrum slides around without the pitch ever going anywhere.
#
# The two plugins are the whole point and neither has another user in
# the tree. `osc::sinsaw' shapes its ramp as (1 - |x|^f) * x, so raising
# `Fine' takes the corners off and the partials fall away faster.
# `osc::buzzer' shapes its as |x|^f * x, which does the opposite: `Coarse'
# up is a sharper corner and a longer harmonic series. One knob thins
# the spectrum, the other thickens it, and what is heard is the gap.
#
# THEY ARE SWEPT BY TWO SLOW LFOs AT DIFFERENT RATES -- 0.082 Hz and
# 0.03 Hz, which are not related to each other by anything. A note held
# for ten seconds never repeats a timbre, and two notes held together
# are at different points in the drift, which is the only reason this
# sounds like an ensemble rather than a chorus effect.
#
# `filt::inkshape' between the oscillators and the combs: a gravity
# low-pass with a squashing term on large steps. Subtracting two ramps
# leaves sharp corners where they disagree, and `Shape' is what takes
# the edge off those without taking the harmonics off everything else.
#
# TWO COMBS TUNED NEAR THE NOTE AND NOT ON IT. `delay::echo' with its
# delay equal to its own buffer length is a resonator: one at 0.505 of
# the note and one at 1.002. The first is an octave down and two cents
# flat; the second is the note itself and two cents sharp. Neither is in
# tune, and that is deliberate -- a comb exactly on the note reinforces
# the harmonics that are already there and does nothing audible, while
# one a couple of cents out beats against them slowly. `Detune' moves
# both.
#
# The feedback is inside `delay::echo' rather than round the graph, so
# there is no cycle here and the file renders the same at any window
# length. `dsp/old/sscomb.dsp', which this comes from, got that right
# already; what it did not have was an envelope -- `play = 1' in its io
# node, so a voice sounded until `poly' stole it.

name "Spectral";
author "Misha Nasledov";
description "Two shaped ramps subtracted and run through combs tuned off the note.";
category "Experiments";

    @fine = 3;
    @fine.widget = 1;
    @fine.min = 0.5;
    @fine.max = 8;
    @fine.label = "Fine";

    @coarse = 25;
    @coarse.widget = 1;
    @coarse.min = 4;
    @coarse.max = 60;
    @coarse.label = "Coarse";

    @drift = 0.5;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 1;
    @drift.label = "Drift";

    @detune = 1.002;
    @detune.widget = 1;
    @detune.min = 0.98;
    @detune.max = 1.02;
    @detune.label = "Detune";

    @ring = 0.7;
    @ring.widget = 1;
    @ring.min = 0;
    @ring.max = 0.95;
    @ring.label = "Ring";

    @cutoff = 0.4;
    @cutoff.widget = 1;
    @cutoff.min = 0.02;
    @cutoff.max = 1;
    @cutoff.label = "Cutoff";

    @res = 0.4;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 1;
    @res.label = "Resonance";

    @shape = 0.3;
    @shape.widget = 1;
    @shape.min = 0;
    @shape.max = 4;
    @shape.label = "Shape";

    @a = 300 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 4000 ms;
    @a.label = "Attack";

    @d = 800 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 6000 ms;
    @d.label = "Decay";

    @s = 70%;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 900 ms;
    @r.widget = 1;
    @r.min = 2 ms;
    @r.max = 8000 ms;
    @r.label = "Release";

    @level = 2;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 3;
    @level.label = "Level";

node ionode {
    out0 = vca->out;
    out1 = vca->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# Two LFOs whose rates have nothing to do with each other. See the head:
# this is what keeps a held chord moving.
node slow1 osc::simple { freq = 0.082; };
node slow2 osc::simple { freq = 0.030; };

node fine env::map {
    in = slow1->out;
    inmin = th_min;
    inmax = th_max;
    outmin = @fine;
    outmax = @fine + @fine * @drift;
};

node coarse env::map {
    in = slow2->out;
    inmin = th_min;
    inmax = th_max;
    outmin = @coarse;
    outmax = @coarse + @coarse * @drift;
};

node thin osc::sinsaw {
    freq = freq->out;
    factor = fine->out;
};

node thick osc::buzzer {
    freq = freq->out;
    factor = coarse->out;
};

# The subtraction, halved so two full-scale ramps cannot make two.
node gap math::sub {
    in0 = thin->out;
    in1 = thick->out;
};

node shaped filt::inkshape {
    in = gap->out * 0.5;
    cutoff = @cutoff;
    res = @res;
    shaper = @shape;
};

# One cycle of each comb's own frequency, in samples. `delay::echo' with
# `delay' equal to `size' reads the sample exactly one buffer back,
# which is what makes it a resonator rather than an echo.
node len1 misc::freq2samples { freq = freq->out * 0.505 * @detune; };
node len2 misc::freq2samples { freq = freq->out * @detune; };

node comb1 delay::echo {
    in = shaped->out;
    size = len1->out;
    delay = len1->out;
    feedback = @ring * 0.43;
    dry = 0;
};

node comb2 delay::echo {
    in = comb1->out;
    size = len2->out;
    delay = len2->out;
    feedback = @ring;
    dry = 0;
};

node vca mixer::mul {
    in0 = comb2->out * @level;
    in1 = env->out;
};

io ionode;
