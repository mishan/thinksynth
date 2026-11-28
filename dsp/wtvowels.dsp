# Vowels -- a voice that moves through A, E, I, O and U.
#
# osc::wavetable over wt_vowels.wav: sixty-four single cycles of a 110 Hz
# voice, its three formants moving from one vowel to the next. `Vowel' is
# where in them a note sits; a slow LFO swings it either side by `Drift',
# so a held chord talks to itself without anything being played. The
# table is band-limited at every pitch, so the top of the keyboard is as
# clean as the bottom.

name "Vowels";
author "Misha Nasledov";
description "A wavetable voice whose formants drift between vowels while a note is held.";
category "Strings and pads";

    @vowel = 0.3;
    @vowel.widget = 1;
    @vowel.min = 0;
    @vowel.max = 1;
    @vowel.label = "Vowel";

    @drift = 0.25;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 0.5;
    @drift.label = "Drift";

    @rate = 0.2;
    @rate.widget = 1;
    @rate.min = 0.02;
    @rate.max = 2;
    @rate.label = "Drift rate (Hz)";

    @a = 300 ms;
    @a.widget = 1;
    @a.min = 1 ms;
    @a.max = 3 s;
    @a.label = "Attack";

    @r = 900 ms;
    @r.widget = 1;
    @r.min = 10 ms;
    @r.max = 5 s;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node lfo osc::simple {
    freq = @rate;
};

node voice osc::wavetable {
    file = "wt_vowels.wav";
    freq = freq->out;
    position = @vowel + @drift * lfo->out;
    reset = ionode->trigger;
};

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node out mixer::mul {
    in0 = voice->out;
    in1 = env->out * ionode->velocity * 0.7;
};

io ionode;
