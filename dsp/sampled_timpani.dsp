# Timpani (sampled) -- recorded, from the `timpani' and `timpani_rolls'
# sample packs.
#
# VSCO 2 Community Edition, Versilian Studios / Sam Gossner; CC0. The
# recordings are not in this repository: scripts/packs.py builds them
# from their source into a release of their own, and the page offers the
# pack for download when something in play needs it. Without it, this
# plays silence.
#
# Five drums, tuned F#2 B2 C#3 E3 F#3 (MIDI 42 47 49 52 54). Every other
# note plays its nearest drum, retuned, as a timpanist retunes with the
# pedal. A hit is a one-shot that rings out whatever the note's length,
# 3 velocity layers.
#
# A roll is the same drums' tremolo recordings, 2 velocity layers, the
# last second looped with a 300 ms crossfade, so it lasts as long as the
# note is held and stops with the release. osc::sample has at most three
# layers and plays a zone at the key's pitch, so the rolls are a second
# osc::sample, and Roll picks between the two: 0 hits, 1 rolls. It is a
# control rather than a length threshold so that a long note can still
# be a hit left to ring, and a piece asks for rolls with `roll = 1' on
# an instrument of their own, or by moving it.

name "Timpani (sampled)";
author "Misha Nasledov";
description "Timpani, hits and rolls, recorded (VSCO 2 Community Edition, Versilian Studios / Sam Gossner).";
category "Drums";

    @roll = 0;
    @roll.widget = 1;
    @roll.min = 0;
    @roll.max = 1;
    @roll.label = "Roll";

    @r = 500 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 4000 ms;
    @r.label = "Roll release";

node ionode {
    channels = 2;
    poly = 16;
    out0 = vca->out;
    out1 = vca->out;
    play = max(max(hit->play * (1 - @roll), env->play * @roll), least->play);
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

node hit osc::sample {
    file = "timpani/timpani_42_2.wav@42 timpani/timpani_47_1.wav@47 timpani/timpani_49_1.wav@49 timpani/timpani_52_1.wav@52 timpani/timpani_54_2.wav@54";
    file2 = "timpani/timpani_42_2.wav@42 timpani/timpani_47_2.wav@47 timpani/timpani_49_2.wav@49 timpani/timpani_52_2.wav@52 timpani/timpani_54_2.wav@54";
    file3 = "timpani/timpani_42_2.wav@42 timpani/timpani_47_3.wav@47 timpani/timpani_49_3.wav@49 timpani/timpani_52_3.wav@52 timpani/timpani_54_3.wav@54";
    freq = freq->out;
    trigger = 1;
    select = ionode->velocity;
    split1 = 0.4;
    split2 = 0.75;
};

node rolls osc::sample {
    file = "timpani_rolls/timpani_rolls_42_1.wav@42 timpani_rolls/timpani_rolls_47_1.wav@47 timpani_rolls/timpani_rolls_49_1.wav@49 timpani_rolls/timpani_rolls_52_1.wav@52 timpani_rolls/timpani_rolls_55_1.wav@55";
    file2 = "timpani_rolls/timpani_rolls_42_2.wav@42 timpani_rolls/timpani_rolls_47_2.wav@47 timpani_rolls/timpani_rolls_49_2.wav@49 timpani_rolls/timpani_rolls_52_2.wav@52 timpani_rolls/timpani_rolls_55_2.wav@55";
    freq = freq->out;
    trigger = ionode->trigger;
    select = ionode->velocity;
    split1 = 0.6;
    split2 = 1;
    loop = 1000 ms;
    xfade = 300 ms;
};

node env env::adsr {
    a = 5 ms;
    d = 0;
    s = 0.5 + 0.5 * ionode->velocity;
    r = @r;
    p = 0.5 + 0.5 * ionode->velocity;
    trigger = ionode->trigger;
};

# Each pack is levelled to the same peak on its own, and at that peak a
# roll is about 9 dB louder than a hit's first 300 ms: 0.6 brings it back
# beside the hits. The lowest drum has one recording, which peaks 20 dB
# under the next drum's middle layer, so the notes it plays (below 45;
# clamp(... * 1000, 0, 1) is "note < 45") take it up with velocity, to
# 6 times at the hardest, as the layers it lacks would.
node vca mixer::mul {
    in0 = hit->out * (0.4 + 0.6 * ionode->velocity) * (1 + 5 * ionode->velocity * clamp((45 - ionode->note) * 1000, 0, 1)) * (1 - @roll) + rolls->out * env->out * 0.6 * @roll;
    in1 = 0.75;
};

io ionode;
