# Orchestral percussion (sampled) -- recorded, from the `orchperc' sample
# pack.
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
# On General MIDI drum notes: 49 crash cymbals, 52 gong (GM's Chinese
# cymbal), 57 suspended cymbal (GM's second crash). Every other note
# plays its nearest instrument, retuned. Each rings out whatever the
# note's length.

name "Orchestral percussion (sampled)";
author "Misha Nasledov";
description "Crash cymbals, gong and suspended cymbal, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
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
    file = "orchperc/orchperc_49_1.wav@49 orchperc/orchperc_52_1.wav@52 orchperc/orchperc_57_1.wav@57";
    file2 = "orchperc/orchperc_49_2.wav@49 orchperc/orchperc_52_2.wav@52 orchperc/orchperc_57_2.wav@57";
    file3 = "orchperc/orchperc_49_3.wav@49 orchperc/orchperc_52_3.wav@52 orchperc/orchperc_57_3.wav@57";
    freq = freq->out;
    trigger = 1;
    select = ionode->velocity;
    split1 = 0.4;
    split2 = 0.75;
};

node vca mixer::mul {
    in0 = smp->out;
    in1 = (0.4 + 0.6 * ionode->velocity) * 0.75;
};

io ionode;
