# French horn (sampled) -- recorded, from the `horn' sample pack.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py fetches them
# from their source, and the page offers the pack for download when
# something in play needs it. Without it, this plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording. 3 velocity layers, chosen by how hard the note is played.
#
# The recordings are trimmed to a few seconds, and the last second
# loops with a 300 ms crossfade, so a held note holds.

name "French horn (sampled)";
author "Misha Nasledov";
description "French horn, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "horn/horn_36_1.wav@36 horn/horn_39_1.wav@39 horn/horn_43_1.wav@43 horn/horn_46_1.wav@46 horn/horn_50_1.wav@50 horn/horn_53_1.wav@53 horn/horn_57_1.wav@57 horn/horn_60_1.wav@60";
    file2 = "horn/horn_36_2.wav@36 horn/horn_39_2.wav@39 horn/horn_43_2.wav@43 horn/horn_46_2.wav@46 horn/horn_50_2.wav@50 horn/horn_53_2.wav@53 horn/horn_57_2.wav@57 horn/horn_60_2.wav@60";
    file3 = "horn/horn_36_2.wav@36 horn/horn_39_2.wav@39 horn/horn_43_2.wav@43 horn/horn_46_2.wav@46 horn/horn_50_3.wav@50 horn/horn_53_2.wav@53 horn/horn_57_2.wav@57 horn/horn_60_3.wav@60";
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
    s = 1;
    r = @r;
    p = 0.5 + 0.5 * ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = smp->out;
    in1 = env->out * 0.5;
};

io ionode;
