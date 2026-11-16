# Oboe (sampled) -- recorded, from the `oboe' sample pack.
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

name "Oboe (sampled)";
author "Misha Nasledov";
description "Oboe, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Leads and stabs";

    @a = 30 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 200 ms;
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
    file = "oboe/oboe_58_1.wav@58 oboe/oboe_62_1.wav@62 oboe/oboe_65_1.wav@65 oboe/oboe_70_1.wav@70 oboe/oboe_74_1.wav@74 oboe/oboe_77_1.wav@77 oboe/oboe_82_1.wav@82 oboe/oboe_86_1.wav@86 oboe/oboe_89_1.wav@89";
    file2 = "oboe/oboe_58_2.wav@58 oboe/oboe_62_2.wav@62 oboe/oboe_65_2.wav@65 oboe/oboe_70_2.wav@70 oboe/oboe_74_2.wav@74 oboe/oboe_77_2.wav@77 oboe/oboe_82_2.wav@82 oboe/oboe_86_2.wav@86 oboe/oboe_89_2.wav@89";
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
    in1 = env->out * 0.55;
};

io ionode;
