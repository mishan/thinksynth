# Upright piano (sampled) -- recorded, from the `upright' sample pack.
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

name "Upright piano (sampled)";
author "Misha Nasledov";
description "Upright piano, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Keys";

    @a = 1 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 400 ms;
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
    file = "upright/upright_24_1.wav@24 upright/upright_31_1.wav@31 upright/upright_36_1.wav@36 upright/upright_43_1.wav@43 upright/upright_48_1.wav@48 upright/upright_55_1.wav@55 upright/upright_60_1.wav@60 upright/upright_67_1.wav@67 upright/upright_72_1.wav@72 upright/upright_79_1.wav@79 upright/upright_84_1.wav@84 upright/upright_91_1.wav@91 upright/upright_96_1.wav@96 upright/upright_103_1.wav@103";
    file2 = "upright/upright_24_1.wav@24 upright/upright_31_2.wav@31 upright/upright_36_2.wav@36 upright/upright_43_2.wav@43 upright/upright_48_2.wav@48 upright/upright_55_2.wav@55 upright/upright_60_2.wav@60 upright/upright_67_2.wav@67 upright/upright_72_2.wav@72 upright/upright_79_2.wav@79 upright/upright_84_2.wav@84 upright/upright_91_2.wav@91 upright/upright_96_2.wav@96 upright/upright_103_2.wav@103";
    file3 = "upright/upright_24_3.wav@24 upright/upright_31_2.wav@31 upright/upright_36_3.wav@36 upright/upright_43_3.wav@43 upright/upright_48_3.wav@48 upright/upright_55_3.wav@55 upright/upright_60_3.wav@60 upright/upright_67_3.wav@67 upright/upright_72_3.wav@72 upright/upright_79_3.wav@79 upright/upright_84_3.wav@84 upright/upright_91_3.wav@91 upright/upright_96_3.wav@96 upright/upright_103_3.wav@103";
    freq = freq->out;
    trigger = ionode->trigger;
    select = ionode->velocity;
    split1 = 0.4;
    split2 = 0.75;
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
