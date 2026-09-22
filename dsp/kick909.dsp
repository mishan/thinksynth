# Kick 909 -- a sine that falls, a click that does not, and a clip.
#
# The drum machine kick that every dance record since has been built on:
# a sine wave whose pitch drops from `Pitch' to `Floor' over `Sweep',
# with a burst of noise on the very front for the beater, and enough
# saturation after the sum that the body flattens into a thump rather
# than a boom. The note number is ignored -- a kick is a kick -- which is
# what `Floor' in hertz rather than a tuning is for.
#
# Both envelopes are env::ad: a drum has nothing to sustain. The note
# ends when the longer one ends, which is the amp's.

name "Kick 909";
author "Misha Nasledov";
description "A falling sine with a noise click and saturation: the dance-floor kick.";
category "Drums";

    @pitch = 190;
    @pitch.widget = 1;
    @pitch.min = 40;
    @pitch.max = 800;
    @pitch.label = "Pitch (Hz)";

    @floor = 46;
    @floor.widget = 1;
    @floor.min = 20;
    @floor.max = 200;
    @floor.label = "Floor (Hz)";

    @sweep = 70 ms;
    @sweep.widget = 1;
    @sweep.min = 1ms;
    @sweep.max = 800ms;
    @sweep.label = "Sweep";

    @decay = 380 ms;
    @decay.widget = 1;
    @decay.min = 20ms;
    @decay.max = 3000ms;
    @decay.label = "Decay";

    @click = 0.5;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Click";

    @drive = 2.4;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 8;
    @drive.label = "Drive";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

# The pitch envelope, 1 at the hit and 0 after `Sweep'.
node penv env::ad {
    a = 0;
    d = @sweep;
};

node body osc::simple {
    freq = @floor + (@pitch - @floor) * penv->out;
    waveform = 0;
};

# The beater: two milliseconds of white noise.
node cenv env::ad {
    a = 0;
    d = 2 ms;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node env env::ad {
    a = 0.5 ms;
    d = @decay;
    p = ionode->velocity;
};

node sum dist::saturate {
    in = body->out + noise->out * cenv->out * @click;
    factor = @drive;
};

node out mixer::mul {
    in0 = sum->out;
    in1 = env->out;
};

io ionode;
