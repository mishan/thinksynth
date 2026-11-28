# Clarinet -- osc::reed at the middle of its bore.
#
# Nothing here draws a wave: the tone is what a waveguide bore and a reed
# make of the breath, so this graph only blows. The reed sits halfway
# down the bore, a cylinder's place, and the odd harmonics come of that.
#
# THE BREATH. An envelope is the player's air: `Breath' at its peak,
# softer for a softer note. The reed does not speak until the breath
# passes about half, so the attack is the bore catching, and a release
# is the tone choking off as the air falls under it rather than a fade.
# `Noise' is the hiss in the air.
#
# THE REED. `Stiffness' is a harder reed: brighter, and it needs more air
# to speak.
#
# NO VIBRATO by default, as a clarinetist plays; `Vibrato' bends the
# pitch and the breath together if it is wanted.

name "Clarinet";
author "Misha Nasledov";
description "A blown clarinet: a waveguide reed and bore, played by its breath.";
category "Leads and stabs";

    @breath = 0.75;
    @breath.widget = 1;
    @breath.min = 0.5;
    @breath.max = 1;
    @breath.label = "Breath";

    @stiffness = 0.5;
    @stiffness.widget = 1;
    @stiffness.min = 0;
    @stiffness.max = 1;
    @stiffness.label = "Stiffness";

    @noise = 0.06;
    @noise.widget = 1;
    @noise.min = 0;
    @noise.max = 0.5;
    @noise.label = "Noise";

    @vibrato = 0;
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
    delay = 300 ms;
    rise = 300 ms;
};

node env env::adsr {
    a = @attack;
    d = 120 ms;
    s = 1;
    r = @release;
    p = 1.4;
    trigger = ionode->trigger;
};

# The breath follows the vibrato's bend, 4% each way at 20 cents.
node reed osc::reed {
    freq = vib->out;
    pressure = @breath * (0.8 + 0.2 * ionode->velocity) * env->out *
               (1 + (vib->out / freq->out - 1) * 3.5);
    stiffness = @stiffness;
    noise = @noise;
    position = 0.5;
};

node mix math::mul {
    in0 = reed->out;
    in1 = @level * 0.6 * (0.4 + 0.6 * ionode->velocity);
};

io ionode;
