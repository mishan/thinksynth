# Conga 808 -- the tom circuit, higher and much shorter.
#
# On a real 808 the three congas and the three toms are the same six
# buttons' worth of circuit with different capacitors: the conga is
# tuned up around 165, 250 and 370 Hz, its decay is a fraction of the
# tom's, and it is struck with a hand rather than a beater so there is
# far less click on the front. Everything else is identical, which is
# why this file is `tom808.dsp' with four defaults moved and is not
# pretending otherwise.
#
# THE SHORT DECAY IS THE POINT. A conga pattern is fast -- offbeats,
# doubles, a roll into the bar -- and a drum that rings for four hundred
# milliseconds turns that into mud. At ninety it is a pattern you can
# hear the shape of, and the difference between this and the tom is
# almost entirely that number.
#
# THE PITCH DROP IS SMALL for the same reason a hand is not a stick: a
# slap tightens the head much less than a beater does, and a conga that
# swoops is a tom. Turned up it becomes one, which is a fair thing to
# want at two in the morning.
#
# Note number for the tuning, same as the tom: play D3, B3 and F#4 on
# one channel for the three the original had.

name "Conga 808";
author "Misha Nasledov";
description "The 808 tom circuit tuned up and cut short: the congas.";

    @drop = 0.08;
    @drop.widget = 1;
    @drop.min = 0;
    @drop.max = 2;
    @drop.label = "Pitch Drop";

    @sweep = 14 ms;
    @sweep.widget = 1;
    @sweep.min = 1ms;
    @sweep.max = 300ms;
    @sweep.label = "Sweep";

    @decay = 95 ms;
    @decay.widget = 1;
    @decay.min = 20ms;
    @decay.max = 1500ms;
    @decay.label = "Decay";

    @click = 0.15;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Slap";

    @drive = 1.4;
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
    d = 1.5 ms;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# Higher than the tom's beater: a hand on a skin is a slap and not a
# knock, and what is left of it above three kilohertz is all of it.
node tick filt::svf {
    in = noise->out * cenv->out;
    cutoff = 3000;
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
