# Sho -- a mouth organ of seventeen bamboo pipes, each with a reed.
#
# Each pipe stands in a wind chest and has a thin brass reed in its
# foot that swings freely through its slot; the pipe above it is tuned
# to the reed and takes the edge off it. A free reed is close to a
# pulse, rich in odd and even partials, and the pipe is a resonance at
# the note. A recording
# of a soprano sheng, the sho's continental relative, puts the second
# partial 16 dB down and the third 32; the sho is the reedier of the
# two, and here at A4, velocity 80, the second is 17 dB down and the
# third 25.
#
# THE BREATH. The reed speaks only as the player's breath builds, so
# a note swells in over `Swell' and fades over `Release', and the tone
# brightens with the swell: the lowpass opens as the reed swings wider.
#
# THE BEATING. The instrument is heard in clusters of five or six
# pipes held together, and what makes a cluster shimmer is that no two
# pipes are quite in tune with each other or hold still. Each pipe
# wanders `Beat' cents either side of its note on misc::drift seeded
# from the note, so every pipe of a cluster wanders its own way; one
# seed for all of them would move them together, and nothing would
# beat. The instrument plays single pipes: a piece holds the cluster.
#
# BOTH WAYS. The sho sounds on the inhale as on the exhale, the reed
# swinging the other way first, and a player turns the breath over
# between chords to keep the reeds sounding. `Inhale' is the other
# direction, a faint change: a squarer pulse, so fewer even partials,
# a slightly darker filter, more air, and a few cents flat. The turn
# is glided, as a breath turns.

name "Sho";
author "Misha Nasledov";
description "A free-reed mouth organ: reedy pipes that swell in, beat against each other and sound on either breath.";
category "Strings and pads";

    @swell = 900 ms;
    @swell.widget = 1;
    @swell.min = 50ms;
    @swell.max = 4000ms;
    @swell.label = "Swell";

    @release = 1200 ms;
    @release.widget = 1;
    @release.min = 100ms;
    @release.max = 5000ms;
    @release.label = "Release";

    @beat = 4;
    @beat.widget = 1;
    @beat.min = 0;
    @beat.max = 20;
    @beat.label = "Beat (cents)";

    @wander = 0.3;
    @wander.widget = 1;
    @wander.min = 0.05;
    @wander.max = 3;
    @wander.label = "Wander Rate (Hz)";

    @bright = 0.5;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 1;
    @bright.label = "Brightness";

    @air = 0.3;
    @air.widget = 1;
    @air.min = 0;
    @air.max = 1;
    @air.label = "Air";

    @inhale = 0;
    @inhale.widget = 1;
    @inhale.min = 0;
    @inhale.max = 1;
    @inhale.step = 1;
    @inhale.label = "Inhale";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    channels = 2;

    # Six pipes held, and six more still fading under the next chord.
    poly = 12;

    out0 = mix->out;
    out1 = mix->out;
    play = env->play;
};

node turn misc::slew {
    in = @inhale;
    time = 150 ms;
};

node wander misc::drift {
    rate = @wander;
    depth = @beat;
    center = 0;
    seed = ionode->note;
};

node freq misc::midi2freq {
    note = ionode->note + (wander->out - turn->out * 4) / 100;
};

node reed osc::blep {
    freq = freq->out;
    waveform = 1;
    pw = 0.38 + 0.06 * turn->out;
};

node env env::adsr {
    a = @swell;
    d = 0;
    s = 0.5 + 0.5 * ionode->velocity;
    r = @release;
    p = 0.5 + 0.5 * ionode->velocity;
    trigger = ionode->trigger;
};

node pipe filt::svf {
    in = reed->out;
    cutoff = freq->out;
    res = 0.8;
};

# The floor keeps a low pipe's partials. The pulse's fundamental is
# above 1 and the band passes it in phase, so the sum is scaled to stay
# inside the -1..1 the filter clamps its input to.
node tone filt::svf {
    in = reed->out * 0.3 + pipe->out_band * 0.5;
    cutoff = clamp(freq->out * (1.5 + (0.5 + 2.5 * @bright) * env->out) *
                   (1 - 0.15 * turn->out), 300, 16000);
    res = 0.3;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node breath filt::svf {
    in = noise->out;
    cutoff = max(freq->out * 2, 200);
    res = 0.5;
};

node mix math::mul {
    in0 = (tone->out_low +
           breath->out_band * @air * 0.15 * (1 + turn->out)) * env->out;
    in1 = @level * 0.12;
};

io ionode;
