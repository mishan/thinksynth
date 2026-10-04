# Drum kit (sampled) -- recorded, from the `drums' sample pack.
#
# Big Rusty Drums, Karoryfer Samples; CC0. The recordings are not in
# this repository: scripts/packs.py fetches them from their source, and
# the page offers the pack for download when something in play needs it.
# Without it, this plays silence.
#
# One osc::sample, its `file' a set of zones -- each recording with the
# note it was made at -- so every note is played from the nearest
# recording. 3 velocity layers, chosen by how hard the note is played.
#
# General MIDI drum notes: 36 kick, 38 snare, 41 43 45 48 toms,
# 42 closed and 46 open hi-hat, 49 crash, 51 ride. Every other
# note plays its nearest drum, retuned.

name "Drum kit (sampled)";
author "Misha Nasledov";
description "Drum kit, recorded (Big Rusty Drums, Karoryfer Samples).";
category "Drums";

node ionode {
    channels = 2;
    poly = 16;
    out0 = vca->out;
    out1 = vca->out;
    play = max(smp->play, least->play);
};

# At least a tenth of a second, so a voice whose file is not
# there yet still lives long enough to be one.
node least env::ad {
    a = 0;
    d = 100 ms;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node smp osc::sample {
    file = "drums/k_36_1.wav@36 drums/sn_center_38_1.wav@38 drums/t22_41_1.wav@41 drums/ht_cl_42_1.wav@42 drums/t18_43_1.wav@43 drums/t15_45_1.wav@45 drums/ht_open_46_1.wav@46 drums/t14_48_1.wav@48 drums/cr_49_1.wav@49 drums/rd_51_1.wav@51";
    file2 = "drums/k_36_2.wav@36 drums/sn_center_38_2.wav@38 drums/t22_41_2.wav@41 drums/ht_cl_42_2.wav@42 drums/t18_43_2.wav@43 drums/t15_45_2.wav@45 drums/ht_open_46_2.wav@46 drums/t14_48_2.wav@48 drums/cr_49_2.wav@49 drums/rd_51_2.wav@51";
    file3 = "drums/k_36_3.wav@36 drums/sn_center_38_3.wav@38 drums/t22_41_3.wav@41 drums/ht_cl_42_3.wav@42 drums/t18_43_3.wav@43 drums/t15_45_3.wav@45 drums/ht_open_46_3.wav@46 drums/t14_48_3.wav@48 drums/cr_49_3.wav@49 drums/rd_51_3.wav@51";
    freq = freq->out;
    trigger = 1;
    select = ionode->velocity;
    split1 = 0.4;
    split2 = 0.75;
};

node vca mixer::mul {
    in0 = smp->out;
    in1 = (0.4 + 0.6 * ionode->velocity) * 0.85;
};

io ionode;
