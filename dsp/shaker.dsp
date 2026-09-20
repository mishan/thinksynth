# Shaker -- a handful of seeds arriving and leaving.
#
# The simplest thing in the tree and the one that has to be right: a
# band of noise with an attack on the front of it. Everything a shaker
# is lives in that attack. A burst of noise with no rise is a hi-hat; a
# few milliseconds of rise is a hand moving, because the seeds arrive at
# the end of the shell over a span of time rather than all at once, and
# the ear hears the difference between three milliseconds and eight as
# the difference between a maraca and a cabasa.
#
# VELOCITY MOVES THE BAND, not only the level. A shaker worked harder is
# brighter, because the seeds hit the shell faster and the shell rings
# higher; `Lift' is how far. It is what keeps a sixteenth-note shaker
# part from being a metronome -- the accented ones sit above the others
# in the mix rather than merely louder, and that is what a hand does.

name "Shaker";
author "Misha Nasledov";
description "A band of noise with a hand's attack, velocity lifting the band.";

    @tone = 5200;
    @tone.widget = 1;
    @tone.min = 800;
    @tone.max = 14000;
    @tone.label = "Band (Hz)";

    @lift = 3200;
    @lift.widget = 1;
    @lift.min = 0;
    @lift.max = 9000;
    @lift.label = "Lift (Hz)";

    @res = 0.65;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Focus";

    @a = 6 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 100ms;
    @a.label = "Hand";

    @d = 60 ms;
    @d.widget = 1;
    @d.min = 5ms;
    @d.max = 800ms;
    @d.label = "Decay";

    @body = 0.25;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1;
    @body.label = "Shell";

node ionode {
    channels = 2;

    # Sixteenths at sixty milliseconds a stroke overlap by a hair.
    poly = 3;

    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The band the seeds ring, and where velocity takes it.
node band filt::svf {
    in = noise->out;
    cutoff = @tone + @lift * ionode->velocity;
    res = @res;
};

node env env::ad {
    a = @a;
    d = @d;
    p = ionode->velocity;
};

# `Shell' is the wood under the seeds: the same band an octave down,
# which is what a gourd adds to a handful of beans.
node shell filt::svf {
    in = noise->out;
    cutoff = (@tone + @lift * ionode->velocity) * 0.5;
    res = 0.5;
};

node out math::mul {
    in0 = band->out_band * 1.1 + shell->out_band * @body;
    in1 = env->out;
};

io ionode;
