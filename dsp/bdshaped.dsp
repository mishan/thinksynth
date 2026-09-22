# BD-10 Shaped -- one sine through a saturator, where bd10.dsp stacks six.
#
# The same kick as `bd10.dsp' -- the same two envelopes, the same pitch
# sweep from `Low Pitch' to `High Pitch' -- with the oscillator swapped.
# There it is an `osc::multiwave' of six partials; here it is one
# `osc::simple' into `dist::saturate', so the harmonics come from the
# `Gain' knob folding a sine rather than from a stack.
#
# `BD-10 Shaped' and not `BD-10', because bd10.dsp is BD-10. Two graphs
# with one name are one row said twice in every chooser, and thSynth's
# tree list is keyed on the name -- so whichever loads second evicts the
# first from it.

name "BD-10 Shaped";
author "Leif Ames";
description "One sine folded by a saturator, with a swept pitch: the shaped kick.";
category "Drums";

    @d = 50 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Pitch Decay";
    @s = 0.1;    # 1 = full, 0 = off
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Pitch Sustain";
    @r = 420 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
    @r.label = "Pitch Release";
    @d2 = 28 ms;
    @d2.widget = 1;
    @d2.min = 0;
    @d2.max = 2000ms;
    @d2.label = "Amp Decay";
    @s2 = 0.6;    # 1 = full, 0 = off
    @s2.widget = 1;
    @s2.min = 0;
    @s2.max = 1;
    @s2.label = "Amp Sustain";
    @r2 = 218 ms;
    @r2.widget = 1;
    @r2.min = 0;
    @r2.max = 5000ms;
    @r2.label = "Amp Release";
    @fhi = 173;
    @fhi.widget = 1;
    @fhi.min = 0;
    @fhi.max = 1000;
    @fhi.label = "High Pitch";
    @flo = 18;
    @flo.widget = 1;
    @flo.min = 0;
    @flo.max = 1000;
    @flo.label = "Low Pitch";
    @gain = 1.7;
    @gain.widget = 1;
    @gain.min = 0;
    @gain.max = 10;
    @gain.label = "Gain";

node ionode {
    out0 = mixer->out;
    out1 = mixer->out;
    channels = 2;
    play = aenv->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node mixer mixer::mul {
    in0 = shaper->out;
    in1 = aenv->out;
};

node penv env::adsr {
    a = 0;
    d = @d;
    s = @s;
    r = @r;
    trigger = 0;
};

node scalc mixer::mul {
    in0 = @s2;
    in1 = ionode->velocity;
};

node aenv env::adsr {
    a = 0.5 ms;
    d = @d2;
    s = scalc->out;
    r = @r2;
    p = ionode->velocity;
    trigger = 0;
};

node map1 env::map {
    in = aenv->out;
    inmin = 0;
    inmax = th_max;
    outmin = 0;
    outmax = 1;
};

node map2 env::map {
    in = penv->out;
    inmin = 0;
    inmax = th_max;
    outmin = @flo;
    outmax = @fhi;
};

node osc osc::simple {
    freq = map2->out;
    amp = aenv->out;
    waveform = 5;
};

node shaper dist::saturate {
    in = osc->out;
    factor = @gain;
};

io ionode;
