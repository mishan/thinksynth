# Kick 808 -- a sine that rings for a second and a half.
#
# The other dance-floor kick, and the opposite bargain from `kick909':
# where the 909 drops fast and hard and is over in a third of a second,
# this one barely moves and does not stop. What the 808 actually had was
# a bridged-T network kicked by a pulse -- a resonator rung once, so the
# pitch sits still and the amplitude decays for as long as the decay
# knob allows. A sine with a long envelope is that, exactly; there is no
# approximation here beyond the click.
#
# THE DECAY IS THE INSTRUMENT. At 200 ms it is a tight kick that sits
# under a bass line; at two seconds it is the 808 everybody means -- the
# note that swallows the bar and has to be arranged around, because a
# kick that rings that long *is* the bass part. There is no setting in
# between that is wrong.
#
# `Tune' is in hertz and the note number is ignored, the way `kick909'
# ignores it: a kick is a kick, and a channel that plays it wants the
# note number free for something else. Around 50 Hz is the record; the
# slider goes low enough to be a pressure wave and high enough to be a
# tom.
#
# The click is two milliseconds of noise, high-passed so it is a tick
# and not a thud -- the trigger pulse leaking through, which on the
# original was a fault and on every record since has been half the
# sound. `Drive' is much gentler than the 909's: this kick is a sine and
# is supposed to stay one, and the saturation is only there to round the
# very top of the attack.

name "Kick 808";
author "Misha Nasledov";
description "A low sine with a long decay and a click: the 808 bass drum.";

    @tune = 52;
    @tune.widget = 1;
    @tune.min = 25;
    @tune.max = 200;
    @tune.label = "Tune (Hz)";

    # A fifth or so above the note for a few tens of milliseconds. The
    # 808's own sweep is small -- the network is rung, not swept -- and
    # this is what stops the attack sounding like a sine fading in.
    @drop = 0.5;
    @drop.widget = 1;
    @drop.min = 0;
    @drop.max = 3;
    @drop.label = "Pitch Drop";

    @sweep = 45 ms;
    @sweep.widget = 1;
    @sweep.min = 1ms;
    @sweep.max = 400ms;
    @sweep.label = "Sweep";

    @decay = 900 ms;
    @decay.widget = 1;
    @decay.min = 100ms;
    @decay.max = 3000ms;
    @decay.label = "Decay";

    @click = 0.3;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Click";

    @drive = 1.3;
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

node penv env::ad {
    a = 0;
    d = @sweep;
};

node body osc::simple {
    freq = @tune * (1 + @drop * penv->out);
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

# The click through a high-pass: what leaks past the network on a real
# one is the edge of the trigger and nothing below it.
node tick filt::svf {
    in = noise->out * cenv->out;
    cutoff = 2500;
    res = 0.2;
};

node env env::ad {
    a = 0.5 ms;
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
