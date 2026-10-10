# Choir -- a section of voices singing one vowel.
#
# A voice is a bright source shaped by the throat, and a section is many
# of them a few cents apart. osc::pad is the section -- every harmonic a
# band rather than a line, its width the spread of pitches across the
# singers -- and filt::vowel the throat: five formants where a tenor
# puts `a', `e', `i', `o' or `u', which stay put while the note moves
# under them. That is what makes it voices and not a filtered pad: the
# resonances belong to the singer, not to the note.
#
# THE MOUTH MOVES. misc::drift takes the vowel a little either side of
# `Vowel', slowly, each voice its own way, so a held chord shifts between
# vowels as a real section does when nobody is quite together. `Gender'
# moves every formant at once: up is a smaller throat, a soprano's `a'.
#
# The two sides are two takes of the same section (osc::pad's out and
# out2), each through a throat of its own, either side of where aux0
# places the note.

name "Choir";
author "Misha Nasledov";
description "A section of voices: PADsynth bands through five formants, the vowel drifting a little under a held chord.";
category "Strings and pads";

    @vowel = 0.3;
    @vowel.widget = 1;
    @vowel.min = 0;
    @vowel.max = 4;
    @vowel.label = "Vowel (a e i o u)";

    # How far either side of `Vowel' each voice's mouth drifts.
    @mouth = 0.4;
    @mouth.widget = 1;
    @mouth.min = 0;
    @mouth.max = 1;
    @mouth.label = "Mouth Drift";

    @gender = 0;
    @gender.widget = 1;
    @gender.min = -1;
    @gender.max = 1;
    @gender.label = "Gender";

    # The spread of the section's pitches, in cents.
    @spread = 30;
    @spread.widget = 1;
    @spread.min = 2;
    @spread.max = 80;
    @spread.label = "Section Spread";

    @breath = 0.04;
    @breath.widget = 1;
    @breath.min = 0;
    @breath.max = 0.3;
    @breath.label = "Breath";

    @width = 0.5;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 1;
    @width.label = "Width";

    @a = 600 ms;
    @a.widget = 1;
    @a.min = 5 ms;
    @a.max = 6000 ms;
    @a.label = "Attack";

    @r = 2500 ms;
    @r.widget = 1;
    @r.min = 50 ms;
    @r.max = 12000 ms;
    @r.label = "Release";

    @level = 0.5;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = left->out0 + right->out0;
    out1 = left->out1 + right->out1;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# Bright, since the formants take away what a throat would: a voice's
# source falls off far slower than a pad's.
node section osc::pad {
    freq = freq->out;
    partials = 64;
    bandwidth = @spread;
    bwscale = 1;
    tilt = -4;
};

node mouth misc::drift {
    rate = 0.2;
    depth = @mouth;
    seed = ionode->note;
};

node breath osc::noise { };

node vl filt::vowel {
    in = section->out + breath->out * @breath;
    vowel = clamp(@vowel + mouth->out, 0, 4);
    gender = @gender;
};

node vr filt::vowel {
    in = section->out2 + breath->out * @breath;
    vowel = clamp(@vowel - mouth->out, 0, 4);
    gender = @gender;
};

node env env::adsr {
    a = @a;
    d = 10 ms;
    s = 0.35 + ionode->velocity * 0.65;
    r = @r;
    p = 0.35 + ionode->velocity * 0.65;
    trigger = ionode->trigger;
};

node left mixer::pan {
    in = vl->out * env->out * @level * 4.5;
    pan = ionode->aux0 - @width;
};

node right mixer::pan {
    in = vr->out * env->out * @level * 4.5;
    pan = ionode->aux0 + @width;
};

io ionode;
