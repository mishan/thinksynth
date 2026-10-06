# Autotune -- a channel's pitch pulled onto the notes of a key.
#
# analysis::yin follows the pitch of what comes in, misc::snap finds the
# nearest note `Key' and `Scale' allow, and delay::pitchshift moves each
# side by the ratio between them. `Speed' is how long the pull takes:
# twenty milliseconds or so is a voice quietly put in tune, and 0 is the
# pull at once, every slide and vibrato turned into steps -- Cher's
# "Believe", and the robot in a thousand songs since.
#
# Where there is no pitch -- breath, a consonant, a drum -- snap's ratio
# is 1 and the channel goes through unshifted. Everything comes out 15 ms
# late, half the shifter's window, shifted or not.

name "Autotune";
author "Misha Nasledov";
description "Pitch correction: a channel pulled onto the nearest note of a key, gently or at once.";
category "Effects";

    @key = 0;
    @key.widget = 1;
    @key.min = 0;
    @key.max = 11;
    @key.step = 1;
    @key.values = "C,C#,D,D#,E,F,F#,G,G#,A,A#,B";
    @key.label = "Key";

    @scale = 1;
    @scale.widget = 1;
    @scale.min = 0;
    @scale.max = 8;
    @scale.step = 1;
    @scale.values = "Chromatic,Major,Minor,Harmonic minor,Dorian,Mixolydian,Major pentatonic,Minor pentatonic,Blues";
    @scale.label = "Scale";

    @speed = 20 ms;
    @speed.widget = 1;
    @speed.min = 0 ms;
    @speed.max = 300 ms;
    @speed.label = "Speed";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = shiftl->out;
    out1 = shiftr->out;
};

node track analysis::yin {
    in = (ionode->in0 + ionode->in1) * 0.5;
};

node snap misc::snap {
    in = track->out;
    key = @key;
    scale = @scale;
};

node pull misc::slew {
    in = snap->ratio;
    time = @speed;
};

node shiftl delay::pitchshift {
    in = ionode->in0;
    ratio = pull->out;
    window = 30 ms;
    mix = @mix;
};

node shiftr delay::pitchshift {
    in = ionode->in1;
    ratio = pull->out;
    window = 30 ms;
    mix = @mix;
};

io ionode;
