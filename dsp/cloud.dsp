# Cloud -- a recording held as a texture, wandering through itself.
#
# osc::grain on a wav: grains of `Grain' milliseconds, `Density' a second,
# each starting near `Position' in the file and read at the pitch the key
# asks for. Enough of them overlapping and there is no grain left to
# hear, only the sound -- sustained for as long as the key is down, which
# a sample played once cannot be. The orchestra hit is the default
# because it is a chord: ten grains a second from its body is a string
# section that never ends.
#
# A misc::drift walks the position about its setting, `Wander' of the
# file either way, a new place every 1 / `Wander Rate' seconds reached
# by a half cosine, so the cloud moves through the recording on its own
# and the same note held for a minute is never the same second twice.
# Both the drift and the cloud are seeded from the note, so a chord is
# several clouds rather than one cloud at several pitches, and a piece
# renders the same twice.
#
# `Jitter' is semitones of random detune per grain, which is what turns
# a clean pitch into a shimmer; a tenth of one is a chorus, a whole one
# is a smear. The grains alternate sides, so the stereo is the cloud's
# own.
#
# Two recordings, and `Blend' between them: the orchestra hit at 0 and
# the kit's ride cymbal at 1, whose long wash of metal is a cloud with no
# pitch in it at all -- grains from it are shimmer and air, played at the
# key like the hit's. A file is a quoted name in a node and not a
# control (see osc::sample), which is why a second recording is a second
# node rather than a knob; the ride is thirteen decibels quieter than the
# hit through its first seventy percent, and is brought up to meet it.

name "Cloud";
author "Misha Nasledov";
description "A granular pad on two recordings: grains drifting through the orchestra hit and the ride.";
category "Strings and pads";

    @position = 0.25;
    @position.widget = 1;
    @position.min = 0;
    @position.max = 1;
    @position.label = "Position";

    @wander = 0.15;
    @wander.widget = 1;
    @wander.min = 0;
    @wander.max = 0.5;
    @wander.label = "Wander";

    @wanderrate = 0.1;
    @wanderrate.widget = 1;
    @wanderrate.min = 0.01;
    @wanderrate.max = 2;
    @wanderrate.label = "Wander Rate (Hz)";

    @spread = 0.04;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 0.5;
    @spread.label = "Spread";

    @grain = 120 ms;
    @grain.widget = 1;
    @grain.min = 5ms;
    @grain.max = 500ms;
    @grain.label = "Grain";

    @density = 30;
    @density.widget = 1;
    @density.min = 1;
    @density.max = 400;
    @density.label = "Density";

    @blend = 0;
    @blend.widget = 1;
    @blend.min = 0;
    @blend.max = 1;
    @blend.label = "Blend (hit to ride)";

    @jitter = 0.08;
    @jitter.widget = 1;
    @jitter.min = 0;
    @jitter.max = 2;
    @jitter.label = "Jitter (semitones)";

    @a = 1500 ms;
    @a.widget = 1;
    @a.min = 5ms;
    @a.max = 10000ms;
    @a.label = "Attack";

    @r = 4000 ms;
    @r.widget = 1;
    @r.min = 20ms;
    @r.max = 20000ms;
    @r.label = "Release";

    # Not `@amp': that name is the channel's own level, which the engine
    # makes for every channel after it has copied the graph's controls, and
    # a graph that declares one too has its knob quietly replaced.
    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = left->out;
    out1 = right->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node place misc::drift {
    rate = @wanderrate;
    depth = @wander;
    center = @position;
    seed = ionode->note;
};

# The orchestra hit is recorded at middle C, which is the root osc::sample
# plays it from in orchhit.dsp; the same here, so the two graphs agree
# about what key the file is in.
node cloud osc::grain {
    file = "orchhit.wav";
    position = place->out;
    spread = @spread;
    size = @grain;
    density = @density;
    jitter = @jitter;
    freq = freq->out;
    root = 261.63;
    seed = ionode->note;
};

node ride osc::grain {
    file = "kit_ride_mid.wav";
    position = place->out;
    spread = @spread;
    size = @grain;
    density = @density;
    jitter = @jitter;
    freq = freq->out;
    root = 261.63;
    seed = ionode->note + 128;
};

node sidel mixer::fade {
    in0 = cloud->out;
    in1 = ride->out * 4;
    fade = @blend;
};

node sider mixer::fade {
    in0 = cloud->out2;
    in1 = ride->out2 * 4;
    fade = @blend;
};

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node left mixer::mul {
    in0 = sidel->out;
    in1 = env->out * @level * ionode->velocity;
};

node right mixer::mul {
    in0 = sider->out;
    in1 = env->out * @level * ionode->velocity;
};

io ionode;
