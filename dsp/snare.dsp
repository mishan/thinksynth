# Snare -- noise through a band-pass, with a tuned body under it.
#
# Every drum in this tree was two squares ring-modulated through white
# noise, because osc::static was the only noise there was and white was
# the only color it had. A snare is not white. The rattle is the wires
# against the bottom head and it sits in a band a kilohertz or two wide,
# which is one band-pass; the thump is the drum itself and it is a short
# tuned note, which is one oscillator. `Balance' is how much of each.
#
# `Color' picks white, pink or brown from osc::noise -- 0, 1, 2. Pink is
# the one that sounds like a snare, which is why it is the default;
# white is a rim shot and brown is a tom.
#
# The two envelopes are both env::ad -- a drum has no sustain and nothing
# to release -- and the note ends when the longer of them does. Each is
# scaled by velocity through its `p', so a quiet hit is a quiet hit and
# not a shorter one.

name "Snare";
author "Misha Nasledov";
description "Noise through a band-pass with a tuned body under it.";
category "Drums";

    @color = 1;
    @color.widget = 1;
    @color.min = 0;
    @color.max = 2;
    @color.label = "Color";

    @tone = 1700;
    @tone.widget = 1;
    @tone.min = 200;
    @tone.max = 8000;
    @tone.label = "Rattle Pitch";

    @res = 0.45;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Rattle Width";

    @a = 1 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 200ms;
    @a.label = "Attack";

    @d = 190 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Rattle Decay";

    @tune = 190;
    @tune.widget = 1;
    @tune.min = 60;
    @tune.max = 600;
    @tune.label = "Body Pitch";

    @bd = 90 ms;
    @bd.widget = 1;
    @bd.min = 0;
    @bd.max = 2000ms;
    @bd.label = "Body Decay";

    @balance = 0.45;
    @balance.widget = 1;
    @balance.min = 0;
    @balance.max = 1;
    @balance.label = "Body";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = env->play;
};

node noise osc::noise {
    color = @color;
    amp = 0.9;
};

# The rattle. `res' widens or narrows it: 0 is most of the spectrum, and
# anything above about 0.9 is a ring rather than a band.
node band filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = @res;
};

node env env::ad {
    a = @a;
    d = @d;
    p = ionode->velocity;
};

# The body: one sine, tuned, and gone before the rattle is.
node drum osc::simple {
    freq = @tune;
    waveform = 0;
};

node denv env::ad {
    a = 0;
    d = @bd;
    p = ionode->velocity;
};

# Two envelopes and a balance, as arithmetic rather than as four nodes.
# The rattle is the drum and goes in whole; `Body' is how much thump to
# put under it, which is why it scales one side and not both.
node mix math::add {
    in0 = band->out_band * env->out;
    in1 = drum->out * denv->out * @balance;
};

io ionode;
