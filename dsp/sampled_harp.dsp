# Harp (sampled) -- recorded, from the `harp' sample pack.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py builds them
# from their source into a release of their own, and the page offers the
# pack for download when something in play needs it. Without it, this
# plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording.

name "Harp (sampled)";
author "Misha Nasledov";
description "Harp, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Plucked";

    @a = 1 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 2500 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 5000 ms;
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
    file = "harp/harp_28_1.wav@28 harp/harp_31_1.wav@31 harp/harp_35_1.wav@35 harp/harp_38_1.wav@38 harp/harp_41_1.wav@41 harp/harp_45_1.wav@45 harp/harp_48_1.wav@48 harp/harp_52_1.wav@52 harp/harp_55_1.wav@55 harp/harp_59_1.wav@59 harp/harp_62_1.wav@62 harp/harp_65_1.wav@65 harp/harp_69_1.wav@69 harp/harp_72_1.wav@72 harp/harp_76_1.wav@76 harp/harp_79_1.wav@79 harp/harp_83_1.wav@83 harp/harp_86_1.wav@86 harp/harp_89_1.wav@89 harp/harp_93_1.wav@93 harp/harp_95_1.wav@95 harp/harp_98_1.wav@98 harp/harp_101_1.wav@101";
    freq = freq->out;
    trigger = ionode->trigger;
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
    in1 = env->out * 0.85;
};

io ionode;
