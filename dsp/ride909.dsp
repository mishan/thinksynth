# Ride -- inharmonic partials, a little hiss, and a band that falls.
#
# A cymbal is a thin metal disc with no reason to vibrate in a harmonic
# series, and that is the entire acoustic story: the modes of a circular
# plate sit at ratios no oscillator will hand you by accident, they are
# dense, and there are hundreds of them. `osc::multiwave' with its
# `pitchadd' gets eight of them -- each partial the last times `Ratio'
# plus `Spread' hertz, which is a series that has no fundamental at any
# setting except `Spread' at zero.
#
# A RIDE IS NOT A CRASH, and the difference is not the decay. It is that
# a ride is struck near the bell with the tip of a stick, so what you
# hear is dominated by a few low modes and there is a *pitch* in it --
# that is the ping, and it is why a ride pattern holds time and a crash
# pattern could not. Fewer partials, tuned lower, less noise, and the
# `Ping' knob puts the first partial back on top of the rest. Take the
# ping out, add hiss and add half a second and this file is
# crash909.dsp, which is what that file is.
#
# THE BAND FALLS AS THE NOTE DECAYS, and that is the one thing here that
# is not obvious. Struck metal loses its high modes first -- they are
# the ones radiating hardest -- so a cymbal gets darker as it rings out,
# and a band-pass that stayed still would give a decaying hiss that
# sounds like a fader rather than a cymbal. `Open' is where the band
# starts and `Close' is where it ends up.
#
# Velocity scales the amplitude and not the length: a ride is struck
# harder, not longer, and the length is the stick leaving the metal.

name "Ride 909";
author "Misha Nasledov";
description "Eight inharmonic partials with a ping and a falling band: the ride cymbal.";
category "Drums";

    @freq = 520;
    @freq.widget = 1;
    @freq.min = 120;
    @freq.max = 2000;
    @freq.label = "Tune (Hz)";

    @ratio = 1.38;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 3;
    @ratio.label = "Ratio";

    @spread = 240;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 2000;
    @spread.label = "Spread (Hz)";

    # The bell. The first partial on its own, put back over the wash,
    # which is what makes a ride a clock rather than a cymbal.
    @ping = 0.45;
    @ping.widget = 1;
    @ping.min = 0;
    @ping.max = 1;
    @ping.label = "Ping";

    @hiss = 0.12;
    @hiss.widget = 1;
    @hiss.min = 0;
    @hiss.max = 1;
    @hiss.label = "Hiss";

    @open = 7000;
    @open.widget = 1;
    @open.min = 1000;
    @open.max = 16000;
    @open.label = "Open (Hz)";

    @close = 2600;
    @close.widget = 1;
    @close.min = 400;
    @close.max = 12000;
    @close.label = "Close (Hz)";

    @res = 0.25;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Band Width";

    @decay = 1400 ms;
    @decay.widget = 1;
    @decay.min = 50ms;
    @decay.max = 6000ms;
    @decay.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node metal osc::multiwave {
    waves = 8;
    freq = @freq;
    amp = 0.9;
    pitchmul = @ratio;
    pitchadd = @spread;
    ampmul = 0.86;
};

node bell osc::simple {
    freq = @freq;
    waveform = 0;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The band, walking down over the same time the note takes. `fenv' runs
# 1 to 0, so the cutoff runs `Open' to `Close' whichever way round the
# two knobs are set -- putting `Close' above `Open' is a cymbal that
# brightens, which no metal does and which is a fair thing to want.
node fenv env::ad {
    a = 0;
    d = @decay;
};

node band filt::svf {
    in = metal->out * 0.5 + noise->out * @hiss * 0.5;
    cutoff = @close + fenv->out * (@open - @close);
    res = @res;
};

# The ping decays faster than the wash: the stick's contact is over
# long before the plate is.
node penv env::ad {
    a = 0.4 ms;
    d = @decay * 0.25;
};

node env env::ad {
    a = 0.4 ms;
    d = @decay;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = band->out_band * 0.9 + bell->out * penv->out * @ping * 0.5;
    in1 = env->out;
};

io ionode;
