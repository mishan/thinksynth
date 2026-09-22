# Clave -- the rimshot's circuit, tuned up and left clean.
#
# Two sticks of hardwood hit together, which on an 808 is the same pulse
# into the same kind of network as the rimshot with the tuning moved to
# 2.5 kHz and nothing stirred into the excitation. Wood has no rattle:
# what makes a clave a clave is that it is one clear pitch, loud, and
# gone. See `rim808.dsp' for why the excitation is an env::ad click and
# the resonance is a filt::svf rather than the `impulse::sine' and
# `filt::resonator' the shape suggests.
#
# `Ring' is the decay, and the useful range is narrow: under about 0.95
# it is a tick, and the top of the slider is a Q of fifty, which rings
# for twenty-five milliseconds at this tuning and is as close to a bell
# as wood gets. The original has no decay knob at all, so both ends here
# are additions.
#
# Half of what makes claves read on a record is that they sit in a band
# nothing else occupies -- above the hats' body, below their air -- so a
# clave pattern stays audible under a full mix without being loud. Move
# `Tone' down into the cowbell's register and that stops being true.

name "Clave";
author "Misha Nasledov";
description "A click into a 2.5 kHz resonator, no noise: the 808 clave.";
category "Drums";

    @tone = 2500;
    @tone.widget = 1;
    @tone.min = 800;
    @tone.max = 8000;
    @tone.label = "Tone (Hz)";

    @ring = 0.99;
    @ring.widget = 1;
    @ring.min = 0.8;
    @ring.max = 0.99;
    @ring.label = "Ring";

    @hit = 0.4 ms;
    @hit.widget = 1;
    @hit.min = 0.1ms;
    @hit.max = 20ms;
    @hit.label = "Strike";

    @decay = 120 ms;
    @decay.widget = 1;
    @decay.min = 10ms;
    @decay.max = 1000ms;
    @decay.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node hit env::ad {
    a = 0;
    d = @hit;
};

node ring filt::svf {
    in = hit->out;
    cutoff = @tone;
    res = @ring;
};

node env env::ad {
    a = 0;
    d = @decay;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = ring->out_band * 0.3 / (1 - @ring);
    in1 = env->out;
};

io ionode;
