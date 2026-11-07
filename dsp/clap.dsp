# Clap -- noise through a band-pass, with a flutter on the front.
#
# A handclap is several hands not quite together: a burst of noise that
# stutters for thirty milliseconds and then a tail. Here the stutter is
# a square wave at `Flutter' hertz gating a short envelope, so the first
# part of the sound is on-off-on-off a few times, and the tail is a
# second, longer envelope under it. Pink noise by default, through a
# filt::svf band-pass at `Tone', which is where the clap sits.
#
# Both envelopes are env::ad and the note ends with the longer. The
# band-pass passes a narrow slice of the noise, so the sum is scaled up
# to sit where the other drums do.

name "Clap";
author "Misha Nasledov";
description "Pink noise through a band-pass, stuttered on the front, with a tail.";
category "Drums";

    @color = 1;
    @color.widget = 1;
    @color.min = 0;
    @color.max = 2;
    @color.label = "Color";

    @tone = 1200;
    @tone.widget = 1;
    @tone.min = 300;
    @tone.max = 6000;
    @tone.label = "Tone (Hz)";

    @res = 0.6;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Width";

    @flutter = 90;
    @flutter.widget = 1;
    @flutter.min = 20;
    @flutter.max = 300;
    @flutter.label = "Flutter (Hz)";

    @front = 30 ms;
    @front.widget = 1;
    @front.min = 5ms;
    @front.max = 200ms;
    @front.label = "Front";

    @tail = 160 ms;
    @tail.widget = 1;
    @tail.min = 20ms;
    @tail.max = 2000ms;
    @tail.label = "Tail";

    @body = 0.6;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1;
    @body.label = "Tail Level";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node noise osc::noise {
    color = @color;
    amp = 1;
};

node bp filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = @res;
};

# The stutter: a square, 0 to 1, gating the front envelope.
node gate osc::simple {
    freq = @flutter;
    waveform = 2;
};

node fenv env::ad {
    a = 0;
    d = @front;
};

node env env::ad {
    a = 0;
    d = @tail;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = bp->out_band;
    in1 = (fenv->out * (gate->out * 0.5 + 0.5) + env->out * @body) * ionode->velocity * 3;
};

io ionode;
