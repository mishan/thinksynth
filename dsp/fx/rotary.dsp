# Rotary -- a Leslie, which is two speakers that are going round.
#
# A rotating speaker cabinet is the only effect on this list that is not
# an electrical trick: it is a horn on a spindle and a drum with a
# rotating baffle under it, in a wooden box, and everything you hear
# from one follows from things physically moving. A microphone in front
# of the cabinet hears each of them arrive nearer and further, so
# louder and quieter -- that is the tremolo -- and sooner and later, so
# sharper and flatter -- that is the chorus. Both at once, and both at
# the rate of the rotation, is why a Leslie cannot be faked with either
# one alone.
#
# TWO ROTORS AT TWO RATES, which is the part most imitations miss. The
# horn is light and spins fast; the drum is heavy and spins slower, and
# they are not geared together, so the two wobbles drift in and out of
# phase forever and the sound never repeats. A single LFO across the
# whole spectrum gives a cabinet with one rotor in it, which is an
# instrument nobody built. `Crossover' is where the horn stops and the
# drum starts -- 800 Hz on a real one.
#
# THE SPIN-UP IS THE SOUND. `Speed' is not a switch between slow and
# fast; it is a target that both rotors chase through misc::slew. Move
# it and the horn arrives first because it is lighter (`Horn Inertia' is
# shorter than `Drum Inertia'), so for two or three seconds the cabinet
# is in two states at once. Every organ record ever made uses that
# transition as a gesture. Holding the knob halfway is not a speed a
# real one has, and is also allowed.
#
# `Move it' is meant literally, and at the time of writing it means from
# the GUI. misc::slew primes its state to the first value it is handed
# -- by design, so that a signal which never moves is a signal it never
# lags -- so a `Speed' that is set once and left is a cabinet already at
# that speed, with no ramp. The ramp is a *change*, which wants a
# composer on the knob; and a sink cannot address one yet.
# docs/GEN_FORMAT.md documents `chanarg = "fx.speed"' for exactly this, but
# the sink's validator in thcGenFile.cpp takes an identifier and refuses
# the dot, so a .gen can set this effect's knobs in its `effect' block
# and cannot automate them.
#
# The stereo is two microphones on opposite sides of the cabinet: what
# is arriving at one is leaving the other, so the right channel's
# tremolo is `1 - ' the left's, which for a unipolar sine is exactly
# half a cycle away, and the chorus taps sit half a cycle apart too.
#
# `fx/chorus.dsp' is the same node doing a different job; see its head.
# Put this on an organ channel -- dsp/organ0.dsp is what it was built
# for.

name "Rotary";
author "Misha Nasledov";
description "A two-rotor rotating speaker with a spin-up: the Leslie.";

    @speed = 0;
    @speed.widget = 1;
    @speed.min = 0;
    @speed.max = 1;
    @speed.label = "Speed";

    @hornslow = 0.8;
    @hornslow.widget = 1;
    @hornslow.min = 0.1;
    @hornslow.max = 4;
    @hornslow.label = "Horn Slow (Hz)";

    @hornfast = 6.6;
    @hornfast.widget = 1;
    @hornfast.min = 1;
    @hornfast.max = 10;
    @hornfast.label = "Horn Fast (Hz)";

    # Slower than the horn at both ends, and not a simple fraction of
    # it, so the two never line up.
    @drumslow = 0.65;
    @drumslow.widget = 1;
    @drumslow.min = 0.1;
    @drumslow.max = 4;
    @drumslow.label = "Drum Slow (Hz)";

    @drumfast = 5.1;
    @drumfast.widget = 1;
    @drumfast.min = 1;
    @drumfast.max = 10;
    @drumfast.label = "Drum Fast (Hz)";

    @hornrise = 900 ms;
    @hornrise.widget = 1;
    @hornrise.min = 10ms;
    @hornrise.max = 6000ms;
    @hornrise.label = "Horn Inertia";

    @drumrise = 2200 ms;
    @drumrise.widget = 1;
    @drumrise.min = 10ms;
    @drumrise.max = 12000ms;
    @drumrise.label = "Drum Inertia";

    @xover = 800;
    @xover.widget = 1;
    @xover.min = 200;
    @xover.max = 3000;
    @xover.label = "Crossover (Hz)";

    @trem = 0.4;
    @trem.widget = 1;
    @trem.min = 0;
    @trem.max = 1;
    @trem.label = "Tremolo";

    @doppler = 1.6 ms;
    @doppler.widget = 1;
    @doppler.min = 0.1ms;
    @doppler.max = 8ms;
    @doppler.label = "Doppler";

    @mix = 0.8;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# The two rotors chasing the knob. Everything about the spin-up is in
# these two nodes having different time constants.
node hornspin misc::slew {
    in = @speed;
    time = @hornrise;
};

node drumspin misc::slew {
    in = @speed;
    time = @drumrise;
};

node hornrate math::add {
    in0 = @hornslow;
    in1 = hornspin->out * (@hornfast - @hornslow);
};

node drumrate math::add {
    in0 = @drumslow;
    in1 = drumspin->out * (@drumfast - @drumslow);
};

# The crossover. One filter a side, and both of its outputs used: the
# high goes round with the horn and the low with the drum.
node splitl filt::svf {
    in = ionode->in0;
    cutoff = @xover;
    res = 0;
};

node splitr filt::svf {
    in = ionode->in1;
    cutoff = @xover;
    res = 0;
};

# The doppler. One chorus per rotor per side, at that rotor's rate,
# `phase' putting the two sides on opposite faces of the cabinet.
node hornl delay::chorus {
    in = splitl->out_high;
    rate = hornrate->out;
    depth = @doppler;
    delay = 3 ms;
    taps = 1;
    mix = 1;
    phase = 0;
};

node hornr delay::chorus {
    in = splitr->out_high;
    rate = hornrate->out;
    depth = @doppler;
    delay = 3 ms;
    taps = 1;
    mix = 1;
    phase = 0.5;
};

node druml delay::chorus {
    in = splitl->out_low;
    rate = drumrate->out;
    depth = @doppler * 0.6;
    delay = 3 ms;
    taps = 1;
    mix = 1;
    phase = 0.25;
};

node drumr delay::chorus {
    in = splitr->out_low;
    rate = drumrate->out;
    depth = @doppler * 0.6;
    delay = 3 ms;
    taps = 1;
    mix = 1;
    phase = 0.75;
};

# And the amplitude half of the same rotation. osc::window is unipolar,
# so `1 - ' it is the other microphone.
node hornlfo osc::window {
    freq = hornrate->out;
    waveform = 0;
};

node drumlfo osc::window {
    freq = drumrate->out;
    waveform = 0;
};

node hornampl mixer::mul {
    in0 = hornl->out;
    in1 = (1 - @trem) + hornlfo->out * @trem;
};

node hornampr mixer::mul {
    in0 = hornr->out;
    in1 = (1 - @trem) + (1 - hornlfo->out) * @trem;
};

# The drum's tremolo is shallower than the horn's: a baffle under a
# cabinet throws much less of the sound at the microphone than a horn
# pointing straight at it does.
node drumampl mixer::mul {
    in0 = druml->out;
    in1 = (1 - @trem * 0.5) + drumlfo->out * @trem * 0.5;
};

node drumampr mixer::mul {
    in0 = drumr->out;
    in1 = (1 - @trem * 0.5) + (1 - drumlfo->out) * @trem * 0.5;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = hornampl->out * 0.9 + drumampl->out * 0.9;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = hornampr->out * 0.9 + drumampr->out * 0.9;
    fade = @mix;
};

io ionode;
