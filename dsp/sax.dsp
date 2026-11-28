# Saxophone -- osc::reed nearer its mouthpiece, with a growl.
#
# The same reed and bore as clarinet.dsp, the reed moved to 0.4 of the
# way along: every harmonic comes in, the second within a few dB of the
# first, toward a cone's sound. Nearer still, where osc::reed's own
# saxophone sits, the loop jumps between a note and its octave as the
# breath moves, so a played patch stays at 0.4.
#
# THE BREATH is an envelope over `Breath', pushed past it at the start,
# the tongue letting the air go, so a low note speaks at once.
#
# THE GROWL. A player hums into the horn as they blow: here a
# 30 Hz flutter in the breath and more noise, `Growl' deep.
#
# THE VIBRATO comes late, and moves the breath with the pitch.

name "Saxophone";
author "Misha Nasledov";
description "A blown saxophone: a waveguide reed and bore, with late vibrato and a growl.";
category "Leads and stabs";

    @breath = 0.75;
    @breath.widget = 1;
    @breath.min = 0.5;
    @breath.max = 1;
    @breath.label = "Breath";

    @stiffness = 0.6;
    @stiffness.widget = 1;
    @stiffness.min = 0;
    @stiffness.max = 1;
    @stiffness.label = "Stiffness";

    @noise = 0.06;
    @noise.widget = 1;
    @noise.min = 0;
    @noise.max = 0.5;
    @noise.label = "Noise";

    @growl = 0;
    @growl.widget = 1;
    @growl.min = 0;
    @growl.max = 1;
    @growl.label = "Growl";

    @vibrato = 18;
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
    delay = 350 ms;
    rise = 400 ms;
};

node env env::adsr {
    a = @attack;
    d = 120 ms;
    s = 1;
    r = @release;
    p = 1.4;
    trigger = ionode->trigger;
};

node flutter osc::simple {
    freq = 30;
    waveform = 3;
};

# The breath follows the vibrato's bend, 4% each way at 20 cents.
node reed osc::reed {
    freq = vib->out;
    pressure = @breath * (0.8 + 0.2 * ionode->velocity) * env->out *
               (1 + (vib->out / freq->out - 1) * 3.5) *
               (1 + 0.25 * @growl * flutter->out);
    stiffness = @stiffness;
    noise = @noise + 0.3 * @growl;
    position = 0.4;
};

node mix math::mul {
    in0 = reed->out;
    in1 = @level * 0.6 * (0.4 + 0.6 * ionode->velocity);
};

io ionode;
