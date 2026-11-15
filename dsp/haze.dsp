# Haze -- a pluck whose every note swells in and lingers, then goes.
#
# A plucked saw and square through a closing filter, as pluck.dsp, and
# then fft::blur on each side: each frequency's level follows the
# pluck's over Blur, so the attack swells in rather than strikes, and
# the note lingers after the pluck has died, a wash of its own
# partials. Random at 0 lets the tail fade as the note; towards 1 its
# phases turn as it falls, and the two sides, seeded apart, shimmer
# wide.
#
# THE VOICE LIVES AS LONG AS ITS TAIL. Its `play' is the envelope's or
# either blur's, whichever is longer: the blurs' stays up while they
# still have sound to come, so a note rings out over Blur however short
# it was played, and the voice ends once its tail is under the floor
# rather than when the key comes up.
#
# Size is 512, a frame of 12 ms: late enough to swell, early enough to
# play from a keyboard. A longer frame blurs pitch more cleanly and
# comes later.

name "Haze";
author "Misha Nasledov";
description "A pluck through a spectral blur: each note swells in and lingers, and its voice lasts as long as its tail.";
category "Plucked";

    @blur = 800 ms;
    @blur.widget = 1;
    @blur.min = 0 ms;
    @blur.max = 5000 ms;
    @blur.label = "Blur";

    @random = 0.3;
    @random.widget = 1;
    @random.min = 0;
    @random.max = 1;
    @random.label = "Random";

    @cutoff = 400;
    @cutoff.widget = 1;
    @cutoff.min = 40;
    @cutoff.max = 8000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 4000;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 10000;
    @depth.label = "Envelope Depth (Hz)";

    @d = 300 ms;
    @d.widget = 1;
    @d.min = 10 ms;
    @d.max = 3000 ms;
    @d.label = "Decay";

    @r = 120 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 2000 ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = bll->out;
    out1 = blr->out;
    play = max(env->play, max(bll->play, blr->play));
};

node freq misc::midi2freq {
    note = ionode->note;
};

node saw osc::simple {
    freq = freq->out;
    waveform = 1;
};

node sqr osc::simple {
    freq = freq->out;
    waveform = 2;
};

node fenv env::adsr {
    a = 0;
    d = 150 ms;
    s = 0;
    r = 10 ms;
    trigger = ionode->trigger;
};

node filt filt::svf {
    in = saw->out * 0.6 + sqr->out * 0.4;
    cutoff = @cutoff + fenv->out * @depth * ionode->velocity;
    res = 0.3;
};

node env env::adsr {
    a = 1 ms;
    d = @d;
    s = 0.1 * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = filt->out_low;
    in1 = env->out;
};

# A side each, seeded apart so that a shimmering tail is wide.
node bll fft::blur {
    in = vca->out;
    size = 512;
    blur = @blur;
    random = @random;
    mix = 1;
    seed = ionode->note;
};

node blr fft::blur {
    in = vca->out;
    size = 512;
    blur = @blur;
    random = @random;
    mix = 1;
    seed = ionode->note + 0.5;
};

io ionode;
