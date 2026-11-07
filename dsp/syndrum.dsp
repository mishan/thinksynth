# Syndrum -- the tom that falls an octave and means to.
#
# A Simmons pad, or the Pollard Syndrum before it: one oscillator, one
# pitch envelope, and a sweep so far past what a drum head does that
# nobody could mistake it for one. That was the point. An acoustic tom
# goes sharp for a few milliseconds as the skin tightens and settles;
# this one starts an octave up and takes a fifth of a second to arrive,
# and the falling whoop is the instrument. Every fill on every record
# between 1980 and 1984 is this sound.
#
# IT IS `tom808.dsp' WITH TWO NUMBERS MOVED, which is worth saying
# plainly: the drop is a whole octave instead of a third, and the sweep
# is ten times longer. The circuits were different and the results are
# not, because what both are is a sine with an envelope on its pitch.
# What is genuinely added here is the noise -- a Simmons pad has a hiss
# under the tone that decays with it, from the analog noise source the
# design used for its snare and never quite kept out of the toms.
#
# `note' for the tuning, so one channel is a kit: play three notes and
# it is a three-piece rack, and `xform::ratchet' on a line of them is a
# fill. The drop is a ratio rather than a frequency, so the sweep is an
# octave from wherever the note is.
#
# The sweep is not linear -- env::ad's decay is a raised cosine, which
# hangs near the top for a moment and then falls away. That is closer to
# the original than a straight line would be: the analog envelope was an
# RC curve, and the ear hears the difference as the drum arriving rather
# than sliding.

name "Syndrum";
author "Misha Nasledov";
description "A sine falling an octave over a fifth of a second, with hiss: the Simmons tom.";
category "Drums";

    @drop = 1;
    @drop.widget = 1;
    @drop.min = 0;
    @drop.max = 3;
    @drop.label = "Pitch Drop";

    @sweep = 190 ms;
    @sweep.widget = 1;
    @sweep.min = 5ms;
    @sweep.max = 1500ms;
    @sweep.label = "Sweep";

    @decay = 480 ms;
    @decay.widget = 1;
    @decay.min = 30ms;
    @decay.max = 3000ms;
    @decay.label = "Decay";

    # The hiss under the tone. It decays with the drum rather than on
    # its own envelope, which is what makes it sound like part of the
    # instrument instead of a snare underneath it.
    @noise = 0.2;
    @noise.widget = 1;
    @noise.min = 0;
    @noise.max = 1;
    @noise.label = "Noise";

    @tone = 2200;
    @tone.widget = 1;
    @tone.min = 300;
    @tone.max = 12000;
    @tone.label = "Noise Corner (Hz)";

    @click = 0.3;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Click";

    @drive = 1.4;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 5;
    @drive.label = "Drive";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node penv env::ad {
    a = 0;
    d = @sweep;
};

node body osc::simple {
    freq = freq->out * (1 + @drop * penv->out);
    waveform = 0;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# High-passed, so the hiss sits over the tone rather than muddying the
# bottom of it, and so the beater at the front has somewhere to be.
node hiss filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = 0.1;
};

node cenv env::ad {
    a = 0;
    d = 2 ms;
};

node env env::ad {
    a = 0.3 ms;
    d = @decay;
    p = ionode->velocity;
};

node sum dist::saturate {
    in = body->out * 0.8 + hiss->out_high * @noise * 0.7 +
         hiss->out_high * cenv->out * @click;
    factor = @drive;
};

node out mixer::mul {
    in0 = sum->out;
    in1 = env->out;
};

io ionode;
