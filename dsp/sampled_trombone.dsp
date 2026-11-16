# Tenor trombone (sampled) -- recorded, from the `trombone' sample pack.
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

name "Tenor trombone (sampled)";
author "Misha Nasledov";
description "Tenor trombone, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Leads and stabs";

    @a = 40 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 250 ms;
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
    file = "trombone/trombone_34_1.wav@34 trombone/trombone_37_1.wav@37 trombone/trombone_39_1.wav@39 trombone/trombone_41_1.wav@41 trombone/trombone_46_1.wav@46 trombone/trombone_50_1.wav@50 trombone/trombone_53_1.wav@53 trombone/trombone_60_1.wav@60 trombone/trombone_61_1.wav@61 trombone/trombone_63_1.wav@63 trombone/trombone_65_1.wav@65";
    file2 = "trombone/trombone_34_2.wav@34 trombone/trombone_37_2.wav@37 trombone/trombone_39_2.wav@39 trombone/trombone_41_2.wav@41 trombone/trombone_46_2.wav@46 trombone/trombone_50_2.wav@50 trombone/trombone_53_2.wav@53 trombone/trombone_60_2.wav@60 trombone/trombone_61_2.wav@61 trombone/trombone_63_2.wav@63 trombone/trombone_65_2.wav@65";
    file3 = "trombone/trombone_34_2.wav@34 trombone/trombone_37_2.wav@37 trombone/trombone_39_3.wav@39 trombone/trombone_41_3.wav@41 trombone/trombone_46_3.wav@46 trombone/trombone_50_3.wav@50 trombone/trombone_53_3.wav@53 trombone/trombone_60_3.wav@60 trombone/trombone_61_3.wav@61 trombone/trombone_63_3.wav@63 trombone/trombone_65_3.wav@65";
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
