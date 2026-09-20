# Triangle -- three partials that share nothing, ringing for a long time.
#
# A bent steel rod struck with a steel beater, which is about as close
# to a pure resonator as percussion gets: no head, no shell, no air to
# damp it, so what is struck rings for seconds. Its modes are set by
# where the bends are and are famously unrelated to each other -- a
# triangle has no pitch, which is exactly why it fits under any chord.
#
# THREE PARTIALS AND NOT A SERIES. `osc::multiwave' with a `pitchadd'
# would give an inharmonic series, but a series is still a series: the
# gaps between its partials follow a rule the ear can hear. Three
# separate oscillators at ratios that are not near any small fraction
# is the other thing, and it is what a triangle sounds like.
#
# THE STRUCK ONE RINGS LONGEST is a rule for every piece of metal: the
# high modes lose their energy into the air first. So the top partial
# here decays fastest and the bottom one slowest, which is why a
# triangle brightens at the hit and settles into a hum.
#
# `Open' is the hand. A triangle held by its cord rings for its own
# decay; a finger on the frame takes it down to a tick, and a part that
# plays both is a part with a hand in it. Velocity is on the beater
# rather than on the ring.

name "Triangle";
author "Misha Nasledov";
description "Three unrelated partials with a long decay: the triangle.";

    @freq = 2350;
    @freq.widget = 1;
    @freq.min = 800;
    @freq.max = 6000;
    @freq.label = "Tune (Hz)";

    @two = 1.66;
    @two.widget = 1;
    @two.min = 1.05;
    @two.max = 3;
    @two.label = "Second Partial";

    @three = 2.71;
    @three.widget = 1;
    @three.min = 1.1;
    @three.max = 6;
    @three.label = "Third Partial";

    @decay = 2600 ms;
    @decay.widget = 1;
    @decay.min = 20ms;
    @decay.max = 8000ms;
    @decay.label = "Decay";

    @open = 1;
    @open.widget = 1;
    @open.min = 0.02;
    @open.max = 1;
    @open.label = "Open";

    @beater = 0.35;
    @beater.widget = 1;
    @beater.min = 0;
    @beater.max = 1.5;
    @beater.label = "Beater";

node ionode {
    channels = 2;

    # It rings for seconds and a part plays it every bar.
    poly = 6;

    out0 = out->out;
    out1 = out->out;
    play = env1->play;
};

node p1 osc::simple {
    freq = @freq;
    waveform = 0;
};

node p2 osc::simple {
    freq = @freq * @two;
    waveform = 0;
};

node p3 osc::simple {
    freq = @freq * @three;
    waveform = 0;
};

# The bottom one outlasts the other two, which is what makes the hit
# bright and the tail a hum.
node env1 env::ad {
    a = 0.2 ms;
    d = @decay * @open;
    p = ionode->velocity;
};

node env2 env::ad {
    a = 0.2 ms;
    d = @decay * @open * 0.7;
    p = ionode->velocity;
};

node env3 env::ad {
    a = 0.2 ms;
    d = @decay * @open * 0.45;
    p = ionode->velocity;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# Steel on steel: two milliseconds of it, and high, because the beater
# is a rod and not a stick.
node tick env::ad {
    a = 0;
    d = 2 ms;
    p = ionode->velocity;
};

node hit filt::svf {
    in = noise->out * tick->out;
    cutoff = 7000;
    res = 0.3;
};

node out math::add {
    in0 = p1->out * env1->out * 0.42 + p2->out * env2->out * 0.3 +
          p3->out * env3->out * 0.22;
    in1 = hit->out_high * @beater;
};

io ionode;
