# Flute -- osc::jet, an air jet across a waveguide bore.
#
# THE BREATH is an envelope over `Breath', pushed past it at the start so
# the jet locks to the bore at once. More breath is brighter, as on a
# real flute; under about 0.55 of it the jet does not speak.
#
# THE JET. `Jet' is its length against the bore's: it moves the tone a
# little, rounder in the middle of its range.
#
# THE AIR. `Noise' is breath in the jet, which a flute has more of than a
# reed; the bore colors it, so it rings at the note.
#
# THE VIBRATO comes late and is the breath's as much as the pitch's.

name "Flute";
author "Misha Nasledov";
description "A blown flute: a waveguide jet and bore, breathy, with late vibrato.";
category "Leads and stabs";

    @breath = 0.8;
    @breath.widget = 1;
    @breath.min = 0.55;
    @breath.max = 1;
    @breath.label = "Breath";

    @jet = 0.32;
    @jet.widget = 1;
    @jet.min = 0.28;
    @jet.max = 0.42;
    @jet.label = "Jet";

    @noise = 0.12;
    @noise.widget = 1;
    @noise.min = 0;
    @noise.max = 0.5;
    @noise.label = "Noise";

    @vibrato = 15;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 60;
    @vibrato.label = "Vibrato (cents)";

    @vibrate = 5;
    @vibrate.widget = 1;
    @vibrate.min = 0.5;
    @vibrate.max = 10;
    @vibrate.label = "Vibrato Rate (Hz)";

    @attack = 15 ms;
    @attack.widget = 1;
    @attack.min = 5ms;
    @attack.max = 1000ms;
    @attack.label = "Attack";

    @release = 60 ms;
    @release.widget = 1;
    @release.min = 20ms;
    @release.max = 2000ms;
    @release.label = "Release";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node vib misc::vibrato {
    in = freq->out;
    rate = @vibrate;
    depth = @vibrato;
    delay = 400 ms;
    rise = 400 ms;
};

node env env::adsr {
    a = @attack;
    d = 120 ms;
    s = 1;
    r = @release;
    p = 1.25;
    trigger = ionode->trigger;
};

# The breath follows the vibrato's bend, 4% each way at 20 cents.
node jet osc::jet {
    freq = vib->out;
    pressure = @breath * (0.8 + 0.2 * ionode->velocity) * env->out *
               (1 + (vib->out / freq->out - 1) * 3.5);
    jet = @jet;
    noise = @noise;
};

node mix math::mul {
    in0 = jet->out;
    in1 = @level * 0.6 * (0.4 + 0.6 * ionode->velocity);
};

io ionode;
