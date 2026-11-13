# Frost -- a bell struck and the strike held.
#
# An FM strike, dxbell's inharmonic operator pair with its index falling
# away, is a sound that is over in a moment. `Catch' into the note,
# fft::freeze takes the last `Size' samples of it and holds them for as
# long as the key is down: the bright instant of the strike, turned into
# a pad. `Random' at 0 holds it as it was, a tone; up from there it
# shimmers. The two sides freeze with seeds of their own, and each note
# with its own, so a chord's shimmer is wide and its notes apart.
#
# The freeze costs nothing until it catches, and then an inverse FFT a
# hop a side: about what sixteen filt::svf cost a note at the default
# Size.
#
# The catch is timed by a saw a hundred seconds long, which starts at -1
# with every note. A note held past a hundred seconds goes quiet: the saw
# comes round, the freeze lets go of the strike, long over by then, and
# catches the silence after it. A slower saw would run under the
# slowest wave osc::simple will play at 44.1k.

name "Frost";
author "Misha Nasledov";
description "A bell's strike, caught a moment in and held as a pad while the key is down.";
category "Strings and pads";

    # Seconds, a plain number: the freeze's timer is arithmetic on it.
    @catch = 0.06;
    @catch.widget = 1;
    @catch.min = 0.01;
    @catch.max = 1;
    @catch.label = "Catch (s)";

    @size = 2048;
    @size.widget = 1;
    @size.min = 256;
    @size.max = 4096;
    @size.label = "Size (samples)";

    @random = 0.3;
    @random.widget = 1;
    @random.min = 0;
    @random.max = 1;
    @random.label = "Random";

    @fade = 150 ms;
    @fade.widget = 1;
    @fade.min = 25 ms;
    @fade.max = 2000 ms;
    @fade.label = "Fade";

    @ratio = 3.5;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 14;
    @ratio.label = "Ratio";

    @index = 5;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 14;
    @index.label = "Index";

    @strike = 400 ms;
    @strike.widget = 1;
    @strike.min = 50 ms;
    @strike.max = 4000 ms;
    @strike.label = "Strike Decay";

    @r = 1200 ms;
    @r.widget = 1;
    @r.min = 50 ms;
    @r.max = 8000 ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vcal->out;
    out1 = vcar->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# One strike, its index and its level falling together.
node hit env::ad {
    a = 1 ms;
    d = @strike;
};

node modop osc::fmop {
    freq = freq->out;
    ratio = @ratio;
};

node carrier osc::fmop {
    freq = freq->out;
    ratio = 1;
    mod = modop->out;
    index = hit->out * @index * ionode->velocity;
};

node strike mixer::mul {
    in0 = carrier->out * 0.22;
    in1 = hit->out;
};

# Seconds since the note began, from a saw that starts at -1 with it, and
# the freeze up from `Catch' on.
node clock osc::simple {
    freq = 0.01;
    amp = 1;
    waveform = 1;
};

node catch math::clamp {
    in = ((clock->out + 1) * 50 - @catch) * 100000;
    lo = 0;
    hi = 1;
};

node fzl fft::freeze {
    in = strike->out;
    freeze = catch->out;
    size = @size;
    random = @random;
    fade = @fade;
    seed = ionode->note;
};

node fzr fft::freeze {
    in = strike->out;
    freeze = catch->out;
    size = @size;
    random = @random;
    fade = @fade;
    seed = ionode->note + 0.5;
};

# The key: held, the pad goes on at a level set by the velocity; let go,
# it falls over `Release'. With no attack or decay the envelope is at its
# sustain from the first sample, so that is where the velocity goes.
node env env::adsr {
    a = 0;
    d = 0;
    s = 0.55 + ionode->velocity * 0.45;
    r = @r;
    trigger = ionode->trigger;
};

node vcal mixer::mul {
    in0 = fzl->out;
    in1 = env->out;
};

node vcar mixer::mul {
    in0 = fzr->out;
    in1 = env->out;
};

io ionode;
