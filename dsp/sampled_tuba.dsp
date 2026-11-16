# Tuba (sampled) -- recorded, from the `tuba' sample pack.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py builds them
# from their source into a release of their own, and the page offers the
# pack for download when something in play needs it. Without it, this
# plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording. 3 velocity layers, chosen by how hard the note is played.
#
# The recordings are trimmed to a few seconds, and the last second
# loops with a 300 ms crossfade, so a held note holds.

name "Tuba (sampled)";
author "Misha Nasledov";
description "Tuba, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Leads and stabs";

    @a = 50 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 300 ms;
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
    file = "tuba/tuba_29_1.wav@29 tuba/tuba_34_1.wav@34 tuba/tuba_39_1.wav@39 tuba/tuba_41_1.wav@41 tuba/tuba_46_1.wav@46 tuba/tuba_50_1.wav@50 tuba/tuba_53_1.wav@53 tuba/tuba_58_1.wav@58 tuba/tuba_62_1.wav@62";
    file2 = "tuba/tuba_29_1.wav@29 tuba/tuba_34_2.wav@34 tuba/tuba_39_2.wav@39 tuba/tuba_41_2.wav@41 tuba/tuba_46_2.wav@46 tuba/tuba_50_2.wav@50 tuba/tuba_53_2.wav@53 tuba/tuba_58_2.wav@58 tuba/tuba_62_1.wav@62";
    file3 = "tuba/tuba_29_1.wav@29 tuba/tuba_34_3.wav@34 tuba/tuba_39_3.wav@39 tuba/tuba_41_3.wav@41 tuba/tuba_46_3.wav@46 tuba/tuba_50_3.wav@50 tuba/tuba_53_3.wav@53 tuba/tuba_58_3.wav@58 tuba/tuba_62_1.wav@62";
    freq = freq->out;
    trigger = ionode->trigger;
    select = ionode->velocity;
    split1 = 0.4;
    split2 = 0.75;
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
