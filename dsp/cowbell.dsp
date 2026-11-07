# Cowbell -- two square waves that do not agree, through a band.
#
# The 808's cowbell is two of the same square-wave oscillators the hats
# use, at 587 Hz and 845 Hz, summed and sent through one band-pass with
# a fast decay on it. That is the entire circuit, and every cowbell on
# every record from 1981 onward is those two numbers.
#
# WHY IT SOUNDS LIKE METAL. 845 over 587 is 1.44, which is close to no
# simple fraction -- not a fifth (1.5), not a fourth (1.33). Two squares
# at an interval like that share almost no partials, so instead of
# fusing into one note with a timbre they stay two notes beating against
# each other, which is what a struck lump of metal does and what a
# tuned drum does not. `Interval' is a knob because the effect is worth
# hearing move: at 1.5 it turns into an organ, at 2 it is a bell, and
# the original is the setting that is not quite either.
#
# The band-pass is what makes it a cowbell rather than two square waves.
# Squares are all odd harmonics forever, and 700 Hz with the resonance
# up throws away everything except the pair of tones and the roughness
# immediately around them.
#
# One envelope, and it is short: the 808's cowbell has no decay control
# at all. `Decay' here goes long enough to be a triangle, which the
# original could not do and which costs nothing to allow.

name "Cowbell";
author "Misha Nasledov";
description "Two squares a rough interval apart through a band-pass: the 808 cowbell.";
category "Drums";

    @tune = 587;
    @tune.widget = 1;
    @tune.min = 200;
    @tune.max = 2000;
    @tune.label = "Tune (Hz)";

    @interval = 1.44;
    @interval.widget = 1;
    @interval.min = 1;
    @interval.max = 3;
    @interval.label = "Interval";

    @tone = 700;
    @tone.widget = 1;
    @tone.min = 200;
    @tone.max = 6000;
    @tone.label = "Band (Hz)";

    @res = 0.7;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Band Width";

    @decay = 260 ms;
    @decay.widget = 1;
    @decay.min = 20ms;
    @decay.max = 2000ms;
    @decay.label = "Decay";

    # The 808 has a click on the front of the cowbell for the same
    # reason it has one on everything: the trigger pulse. A few
    # milliseconds of the band-pass wide open.
    @click = 0.35;
    @click.widget = 1;
    @click.min = 0;
    @click.max = 1;
    @click.label = "Click";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node osc1 osc::simple {
    freq = @tune;
    waveform = 2;
};

node osc2 osc::simple {
    freq = @tune * @interval;
    waveform = 2;
};

node band filt::svf {
    in = osc1->out * 0.5 + osc2->out * 0.5;
    cutoff = @tone;
    res = @res;
};

node cenv env::ad {
    a = 0;
    d = 3 ms;
};

node env env::ad {
    a = 0;
    d = @decay;
    p = ionode->velocity;
};

# The band output for the body and a few milliseconds of the unfiltered
# pair for the strike, which is the edge the band-pass has taken off.
node out mixer::mul {
    in0 = band->out_band * 0.9 +
          (osc1->out * 0.5 + osc2->out * 0.5) * cenv->out * @click;
    in1 = env->out;
};

io ionode;
