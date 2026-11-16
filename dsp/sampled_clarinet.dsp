# Clarinet (sampled) -- recorded, from the `clarinet' sample pack.
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

name "Clarinet (sampled)";
author "Misha Nasledov";
description "Clarinet, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "clarinet/clarinet_50_1.wav@50 clarinet/clarinet_53_1.wav@53 clarinet/clarinet_58_1.wav@58 clarinet/clarinet_62_1.wav@62 clarinet/clarinet_65_1.wav@65 clarinet/clarinet_70_1.wav@70 clarinet/clarinet_74_1.wav@74 clarinet/clarinet_77_1.wav@77 clarinet/clarinet_82_1.wav@82 clarinet/clarinet_86_1.wav@86 clarinet/clarinet_89_1.wav@89";
    file2 = "clarinet/clarinet_50_2.wav@50 clarinet/clarinet_53_2.wav@53 clarinet/clarinet_58_2.wav@58 clarinet/clarinet_62_2.wav@62 clarinet/clarinet_65_2.wav@65 clarinet/clarinet_70_2.wav@70 clarinet/clarinet_74_2.wav@74 clarinet/clarinet_77_2.wav@77 clarinet/clarinet_82_2.wav@82 clarinet/clarinet_86_2.wav@86 clarinet/clarinet_89_2.wav@89";
    file3 = "clarinet/clarinet_50_3.wav@50 clarinet/clarinet_53_3.wav@53 clarinet/clarinet_58_3.wav@58 clarinet/clarinet_62_3.wav@62 clarinet/clarinet_65_3.wav@65 clarinet/clarinet_70_3.wav@70 clarinet/clarinet_74_3.wav@74 clarinet/clarinet_77_3.wav@77 clarinet/clarinet_82_3.wav@82 clarinet/clarinet_86_3.wav@86 clarinet/clarinet_89_3.wav@89";
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
