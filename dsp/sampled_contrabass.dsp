# Contrabass (sampled) -- recorded, from the `contrabass' sample pack.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py builds them
# from their source into a release of their own, and the page offers the
# pack for download when something in play needs it. Without it, this
# plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording. 2 velocity layers, chosen by how hard the note is played.
#
# The recordings are trimmed to a few seconds, and the last second
# loops with a 300 ms crossfade, so a held note holds.

name "Contrabass (sampled)";
author "Misha Nasledov";
description "Contrabass, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Strings and pads";

    @a = 80 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 350 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 4000 ms;
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

node smp osc::sample {
    file = "contrabass/contrabass_28_1.wav@28 contrabass/contrabass_30_1.wav@30 contrabass/contrabass_31_1.wav@31 contrabass/contrabass_34_1.wav@34 contrabass/contrabass_36_1.wav@36 contrabass/contrabass_38_1.wav@38 contrabass/contrabass_40_1.wav@40 contrabass/contrabass_42_1.wav@42 contrabass/contrabass_44_1.wav@44 contrabass/contrabass_45_1.wav@45 contrabass/contrabass_49_1.wav@49 contrabass/contrabass_52_1.wav@52 contrabass/contrabass_56_1.wav@56 contrabass/contrabass_59_1.wav@59";
    file2 = "contrabass/contrabass_28_2.wav@28 contrabass/contrabass_30_2.wav@30 contrabass/contrabass_31_2.wav@31 contrabass/contrabass_34_2.wav@34 contrabass/contrabass_36_2.wav@36 contrabass/contrabass_38_2.wav@38 contrabass/contrabass_40_2.wav@40 contrabass/contrabass_42_2.wav@42 contrabass/contrabass_44_2.wav@44 contrabass/contrabass_45_2.wav@45 contrabass/contrabass_49_2.wav@49 contrabass/contrabass_52_2.wav@52 contrabass/contrabass_56_2.wav@56 contrabass/contrabass_59_2.wav@59";
    freq = freq->out;
    trigger = ionode->trigger;
    select = ionode->velocity;
    split1 = 0.6;
    split2 = 1;
    loop = 1000 ms;
    xfade = 300 ms;
};

node env env::adsr {
    a = @a;
    d = 0;
    s = 0.5 + 0.5 * ionode->velocity;
    r = @r;
    p = 0.5 + 0.5 * ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = smp->out;
    in1 = env->out * 0.7;
};

io ionode;
