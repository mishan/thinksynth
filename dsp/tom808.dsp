# Tom 808 -- a sine that falls, tuned by the note number.
#
# Three toms on the front panel of an 808, and one circuit behind them:
# a network rung by the trigger, with a resistor picked by which button
# was pressed. Here the note number is the resistor. Play E2, A2 and E3
# on one channel and that is the low, mid and high tom of the original;
# play anything else and it is the tom the original did not have.
#
# THAT IS THE WHOLE REASON THIS TAKES A NOTE and `kick808' does not. A
# kick is one sound and wants the note number free; a tom fill is three
# sounds that differ only in pitch, and a channel per tom is three
# copies of a graph where one will do. `xform::ratchet' and a scale of
# three notes is a fill.
#
# The pitch drop is bigger than the kick's and much shorter. A drum head
# struck hard goes sharp for a moment as the skin tightens, and that
# moment is the difference between a tom and a sine with an envelope on
# it. Past about a whole tone of drop it stops being a tom and becomes
# the sound a synthesizer makes when it is pretending, which is also
# worth having -- see `syndrum.dsp', which is that on purpose.
#
# The click is the beater. Noise, two milliseconds, high-passed so it
# lands on the attack rather than thickening the body.

name "Tom 808";
author "Misha Nasledov";
description "A falling sine with a beater click, tuned by the note: the 808 toms.";

    # A ratio on top of the note, so the drop is the same interval
    # wherever the tom is tuned. 0.25 is about a major third.
    @drop = 0.25;
    @drop.widget = 1;
    @drop.min = 0;
    @drop.max = 2;
    @drop.label = "Pitch Drop";

    @sweep = 28 ms;
    @sweep.widget = 1;
    @sweep.min = 1ms;
    @sweep.max = 300ms;
    @sweep.label = "Sweep";

    @decay = 420 ms;
    @decay.widget = 1;
    @decay.min = 30ms;
    @decay.max = 3000ms;
    @decay.label = "Decay";

    @click = 0.35;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Click";

    @drive = 1.2;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 4;
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

node cenv env::ad {
    a = 0;
    d = 2 ms;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node tick filt::svf {
    in = noise->out * cenv->out;
    cutoff = 1800;
    res = 0.2;
};

node env env::ad {
    a = 0.3 ms;
    d = @decay;
    p = ionode->velocity;
};

node sum dist::saturate {
    in = body->out * 0.9 + tick->out_high * @click;
    factor = @drive;
};

node out mixer::mul {
    in0 = sum->out;
    in1 = env->out;
};

io ionode;
