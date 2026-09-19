# Clav -- a narrow pulse through a resonant band, and nothing held.
#
# A Hohner Clavinet is a string struck by a rubber pad and read by a
# magnetic pickup, which is a guitar rather than a keyboard. Two things
# follow, and they are the whole instrument.
#
# THE PULSE IS NARROW. A pickup under a short length of struck string
# hears almost all of the harmonic series at once -- the spectrum is
# nearly flat for a dozen partials, which is what a pulse of about a
# tenth of a cycle gives you and what a square does not. At `Width' 0.5
# this stops being a clav and becomes a reed organ; the useful range is
# the bottom fifth of the slider, and the rest is there because the
# sound at 0.3 is worth having under a different name.
#
# THE BAND IS THE PICKUP. What stops that flat series being a buzz is
# that a magnetic pickup is not a microphone: it is resonant, and it
# hears a band a kilohertz or two wide and very little either side. One
# resonant band-pass at 1.5 kHz is a closer model of that than a
# low-pass with an envelope, and it is why this graph has no filter
# envelope at all. The pickup does not move.
#
# VELOCITY GOES TO THE PULSE and only partly to the amplitude. Hitting a
# clavinet key harder drives the string further, which adds harmonics
# well before it adds much level -- the same bargain the DX graphs make
# with the modulation index, and audible for the same reason.
#
# No sustain to speak of. A clavinet note is over almost as soon as it
# starts; what makes a clav part is the playing, not the envelope, which
# is why `xform::ratchet' and a short `hold' get more out of this file
# than any of its knobs do.

name "Clav";
author "Misha Nasledov";
description "A narrow pulse through a resonant band-pass: the Clavinet.";

    @pw = 0.1;
    @pw.widget = 1;
    @pw.min = 0.02;
    @pw.max = 0.5;
    @pw.label = "Width";

    @tone = 1500;
    @tone.widget = 1;
    @tone.min = 300;
    @tone.max = 6000;
    @tone.label = "Pickup (Hz)";

    @res = 0.72;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Pickup Q";

    # How much of the band-passed pickup goes out against the raw pulse.
    # All of it is the neck pickup; a little of the pulse underneath is
    # the bridge one, which is where the bite lives.
    @body = 0.25;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1;
    @body.label = "Bridge";

    @a = 1 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 100ms;
    @a.label = "Attack";

    @d = 230 ms;
    @d.widget = 1;
    @d.min = 10ms;
    @d.max = 2000ms;
    @d.label = "Decay";

    # See strings.dsp: a sustain of exactly zero ends the note and a
    # held key then restarts it.
    @s = 0.1;
    @s.widget = 1;
    @s.min = 0.02;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 60 ms;
    @r.widget = 1;
    @r.min = 5ms;
    @r.max = 1000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# Velocity on the oscillator's own amplitude, so a hard note is a wider
# swing into the band below and not merely a louder one out of it.
node osc osc::simple {
    freq = freq->out;
    waveform = 2;
    pw = @pw;
    amp = 0.35 + ionode->velocity * 0.65;
};

node pickup filt::svf {
    in = osc->out;
    cutoff = @tone;
    res = @res;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = 0.5 + ionode->velocity * 0.5;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = pickup->out_band * 0.8 + osc->out * @body * 0.35;
    in1 = env->out;
};

io ionode;
