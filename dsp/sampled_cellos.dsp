# Cello section (sampled) -- recorded, from the `cellos' sample pack.
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

name "Cello section (sampled)";
author "Misha Nasledov";
description "Cello section, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Strings and pads";

    @a = 120 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 450 ms;
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
    file = "cellos/cellos_36_1.wav@36 cellos/cellos_40_1.wav@40 cellos/cellos_43_1.wav@43 cellos/cellos_47_1.wav@47 cellos/cellos_50_1.wav@50 cellos/cellos_53_1.wav@53 cellos/cellos_57_1.wav@57 cellos/cellos_60_1.wav@60 cellos/cellos_64_1.wav@64 cellos/cellos_67_1.wav@67 cellos/cellos_71_1.wav@71 cellos/cellos_74_1.wav@74 cellos/cellos_77_1.wav@77";
    file2 = "cellos/cellos_36_2.wav@36 cellos/cellos_40_2.wav@40 cellos/cellos_43_2.wav@43 cellos/cellos_47_2.wav@47 cellos/cellos_50_2.wav@50 cellos/cellos_53_2.wav@53 cellos/cellos_57_2.wav@57 cellos/cellos_60_2.wav@60 cellos/cellos_64_2.wav@64 cellos/cellos_67_2.wav@67 cellos/cellos_71_2.wav@71 cellos/cellos_74_2.wav@74 cellos/cellos_77_2.wav@77";
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
    in1 = env->out * 0.75;
};

io ionode;
