# Kit Ride -- the cymbal you play on rather than hit.
#
# The same alloy as kit_hat.dsp and the opposite job. A ride is struck
# on its shoulder every beat and never stops: the partials run for
# seconds, so what the ear hears is not a series of hits but one wash
# with a stick pattern on top of it. That means a long decay and a
# `poly' big enough to let the previous strokes go on ringing --
# anything less and the ride ticks instead of rides.
#
# THE BELL is the other half. Struck near the cup, a ride gives a clear
# high partial that the wash does not have, and a player gets at it by
# hitting harder and nearer the middle. So velocity brings the bell up
# and not the wash: at 40 it is a stick on a plate, at 120 it is a bell
# over it. Cubed, because a ride played at half stroke should have
# almost no bell in it at all.
#
# The bell is a narrow band rung by the stick and left to ring on its
# own -- two and a half octaves above the alloy, which is roughly where
# a twenty-inch ride's bell sits. `filt::svf' rather than
# `filt::resonator' because its cutoff is in hertz and exact; see
# rim808.dsp for the rest of that argument.

name "Kit Ride";
author "Misha Nasledov";
description "A long wash of inharmonic partials with a bell velocity brings out: the ride cymbal.";
category "Drums";

    @freq = 540;
    @freq.widget = 1;
    @freq.min = 150;
    @freq.max = 2000;
    @freq.label = "Tune (Hz)";

    @pmul = 1.37;
    @pmul.widget = 1;
    @pmul.min = 1;
    @pmul.max = 3;
    @pmul.label = "Partial Ratio";

    @spread = 230;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 900;
    @spread.label = "Spread (Hz)";

    @tone = 6500;
    @tone.widget = 1;
    @tone.min = 1000;
    @tone.max = 16000;
    @tone.label = "Edge (Hz)";

    @wash = 2200 ms;
    @wash.widget = 1;
    @wash.min = 200ms;
    @wash.max = 6000ms;
    @wash.label = "Wash";

    @bell = 3000;
    @bell.widget = 1;
    @bell.min = 800;
    @bell.max = 9000;
    @bell.label = "Bell (Hz)";

    @bd = 900 ms;
    @bd.widget = 1;
    @bd.min = 50ms;
    @bd.max = 4000ms;
    @bd.label = "Bell Decay";

    @cup = 0.9;
    @cup.widget = 1;
    @cup.min = 0;
    @cup.max = 2;
    @cup.label = "Bell Level";

    @air = 0.35;
    @air.widget = 1;
    @air.min = 0;
    @air.max = 1.5;
    @air.label = "Air";

node ionode {
    channels = 2;

    # A ride is played on: the stroke before last is still sounding when
    # this one lands, and a cymbal that cuts itself off is a hi-hat.
    poly = 6;

    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node metal osc::multiwave {
    waves = 7;
    freq = @freq;
    amp = 0.9;
    pitchmul = @pmul;
    pitchadd = @spread;
    ampmul = 0.86;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node edge filt::svf {
    in = metal->out * 0.4 + noise->out * @air * 0.3;
    cutoff = @tone;
    res = 0.2;
};

# The stick on the shoulder: three milliseconds, and the only thing in
# the graph that is not still sounding a second later.
node tip env::ad {
    a = 0;
    d = 3 ms;
};

node ring filt::svf {
    in = noise->out * tip->out * 0.8;
    cutoff = @bell;
    res = 0.985;
};

node benv env::ad {
    a = 0.2 ms;
    d = @bd;
};

node env env::ad {
    a = 0.2 ms;
    d = @wash;
    p = ionode->velocity;
};

# Cubed: the bell is what a player reaches for at the top of the stroke
# and it should be absent below the middle of it.
node how math::mul {
    in0 = ionode->velocity * ionode->velocity;
    in1 = ionode->velocity;
};

node out math::add {
    in0 = (edge->out_band * 0.7 + edge->out_high * 0.6) * env->out;
    in1 = ring->out_band * benv->out * how->out * @cup * 1.2;
};

io ionode;
