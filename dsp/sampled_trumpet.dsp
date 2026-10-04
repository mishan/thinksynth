# Trumpet (sampled) -- recorded, from the `trumpet' sample pack.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py fetches them
# from their source, and the page offers the pack for download when
# something in play needs it. Without it, this plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording. 2 velocity layers, chosen by how hard the note is played.
#
# The recordings are trimmed to a few seconds, and the last second
# loops with a 300 ms crossfade, so a held note holds.

name "Trumpet (sampled)";
author "Misha Nasledov";
description "Trumpet, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Leads and stabs";

    @a = 20 ms;
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
    file = "trumpet/trumpet_53_1.wav@53 trumpet/trumpet_57_1.wav@57 trumpet/trumpet_60_1.wav@60 trumpet/trumpet_63_1.wav@63 trumpet/trumpet_67_1.wav@67 trumpet/trumpet_70_1.wav@70 trumpet/trumpet_74_1.wav@74 trumpet/trumpet_77_1.wav@77 trumpet/trumpet_81_1.wav@81 trumpet/trumpet_84_1.wav@84";
    file2 = "trumpet/trumpet_53_2.wav@53 trumpet/trumpet_57_2.wav@57 trumpet/trumpet_60_2.wav@60 trumpet/trumpet_63_2.wav@63 trumpet/trumpet_67_2.wav@67 trumpet/trumpet_70_2.wav@70 trumpet/trumpet_74_2.wav@74 trumpet/trumpet_77_2.wav@77 trumpet/trumpet_81_2.wav@81 trumpet/trumpet_84_2.wav@84";
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
    s = 1;
    r = @r;
    p = 0.5 + 0.5 * ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = smp->out;
    in1 = env->out * 0.6;
};

io ionode;
