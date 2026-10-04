# Violin section (sampled) -- recorded, from the `violins' sample pack.
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

name "Violin section (sampled)";
author "Misha Nasledov";
description "Violin section, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "violins/violins_55_1.wav@55 violins/violins_57_1.wav@57 violins/violins_59_1.wav@59 violins/violins_62_1.wav@62 violins/violins_66_1.wav@66 violins/violins_69_1.wav@69 violins/violins_72_1.wav@72 violins/violins_76_1.wav@76 violins/violins_79_1.wav@79 violins/violins_83_1.wav@83 violins/violins_86_1.wav@86";
    file2 = "violins/violins_55_2.wav@55 violins/violins_57_2.wav@57 violins/violins_59_2.wav@59 violins/violins_62_2.wav@62 violins/violins_66_2.wav@66 violins/violins_69_2.wav@69 violins/violins_72_2.wav@72 violins/violins_76_2.wav@76 violins/violins_79_2.wav@79 violins/violins_83_2.wav@83 violins/violins_86_2.wav@86";
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
    in1 = env->out * 0.9;
};

io ionode;
