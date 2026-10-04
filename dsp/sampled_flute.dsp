# Flute (sampled) -- recorded, from the `flute' sample pack.
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

name "Flute (sampled)";
author "Misha Nasledov";
description "Flute, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "flute/flute_60_1.wav@60 flute/flute_64_1.wav@64 flute/flute_69_1.wav@69 flute/flute_72_1.wav@72 flute/flute_76_1.wav@76 flute/flute_81_1.wav@81 flute/flute_84_1.wav@84 flute/flute_88_1.wav@88 flute/flute_93_1.wav@93 flute/flute_96_1.wav@96";
    file2 = "flute/flute_60_2.wav@60 flute/flute_64_2.wav@64 flute/flute_69_2.wav@69 flute/flute_72_2.wav@72 flute/flute_76_2.wav@76 flute/flute_81_2.wav@81 flute/flute_84_1.wav@84 flute/flute_88_1.wav@88 flute/flute_93_2.wav@93 flute/flute_96_1.wav@96";
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
    in1 = env->out * 3.2;
};

io ionode;
