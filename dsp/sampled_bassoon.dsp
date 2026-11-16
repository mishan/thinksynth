# Bassoon (sampled) -- recorded, from the `bassoon' sample pack.
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

name "Bassoon (sampled)";
author "Misha Nasledov";
description "Bassoon, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "bassoon/bassoon_34_1.wav@34 bassoon/bassoon_37_1.wav@37 bassoon/bassoon_41_1.wav@41 bassoon/bassoon_45_1.wav@45 bassoon/bassoon_46_1.wav@46 bassoon/bassoon_48_1.wav@48 bassoon/bassoon_55_1.wav@55 bassoon/bassoon_60_1.wav@60 bassoon/bassoon_63_1.wav@63 bassoon/bassoon_68_1.wav@68 bassoon/bassoon_69_1.wav@69 bassoon/bassoon_72_1.wav@72 bassoon/bassoon_75_2.wav@75";
    file2 = "bassoon/bassoon_34_2.wav@34 bassoon/bassoon_37_2.wav@37 bassoon/bassoon_41_2.wav@41 bassoon/bassoon_45_2.wav@45 bassoon/bassoon_46_2.wav@46 bassoon/bassoon_48_2.wav@48 bassoon/bassoon_55_2.wav@55 bassoon/bassoon_60_2.wav@60 bassoon/bassoon_63_2.wav@63 bassoon/bassoon_68_2.wav@68 bassoon/bassoon_69_2.wav@69 bassoon/bassoon_72_2.wav@72 bassoon/bassoon_75_2.wav@75";
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
    in1 = env->out * 0.45;
};

io ionode;
