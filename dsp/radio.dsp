# Radio -- a shortwave set being tuned, as an instrument.
#
# Three things a shortwave receiver does between stations. A HISS that a
# band-pass sweeps as the dial moves, the dial being a slow random walk.
# A WHISTLE where two carriers beat against each other: a sine at the
# note, wandering a little, louder as the dial nears it. And FADING, the
# ionosphere turning the whole thing up and down every few seconds.
# Play a chord of it and it is several stations at once; hold one and
# the dial keeps moving.

name "Radio";
author "Misha Nasledov";
description "A shortwave receiver between stations: swept hiss, a wandering heterodyne whistle and fading.";
category "Experiments";

    @dial = 0.3;
    @dial.widget = 1;
    @dial.min = 0.02;
    @dial.max = 3;
    @dial.label = "Dial Speed (Hz)";

    @whistle = 0.4;
    @whistle.widget = 1;
    @whistle.min = 0;
    @whistle.max = 1;
    @whistle.label = "Whistle";

    @wander = 40;
    @wander.widget = 1;
    @wander.min = 0;
    @wander.max = 300;
    @wander.label = "Whistle Wander (cents)";

    @fade = 0.5;
    @fade.widget = 1;
    @fade.min = 0;
    @fade.max = 1;
    @fade.label = "Fading";

    @a = 300 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 4000ms;
    @a.label = "Attack";

    @r = 800 ms;
    @r.widget = 1;
    @r.min = 70ms;
    @r.max = 6000ms;
    @r.label = "Release";

    @level = 0.6;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note;
};

# Where the dial is, -1 to 1, and so where the hiss's band sits: from
# 300 Hz to 4.8 kHz, four octaves.
node dial misc::drift {
    rate = @dial;
    depth = 1;
    center = 0;
    seed = ionode->note;
};

node hiss osc::noise {
    color = 0;
};

node band filt::svf {
    in = hiss->out;
    cutoff = 1200 * exp2(dial->out * 2);
    res = 0.85;
};

node wander misc::drift {
    rate = @dial * 2;
    depth = @wander;
    center = 0;
    seed = ionode->note + 1000;
};

node carrier osc::simple {
    freq = pitch->out * exp2(wander->out / 1200);
    waveform = 0;
};

node fading misc::drift {
    rate = 0.25;
    depth = 0.5;
    center = 0.5;
    seed = ionode->note + 2000;
};

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = 1;
    r = @r;
    trigger = ionode->trigger;
};

# The whistle loudest where the dial is nearest the middle, and the
# whole of it under the fading.
node mix math::add {
    in0 = band->out_band * 5;
    in1 = carrier->out * @whistle * (1 - abs(dial->out)) * 1.6;
};

node vca mixer::mul {
    in0 = mix->out * @level * (1 - @fade + @fade * fading->out);
    in1 = env->out;
};

io ionode;
