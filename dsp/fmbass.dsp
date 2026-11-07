# FM Bass -- two operators at 1:1, and the index is the whole sound.
#
# The preset everybody means by "that DX bass": one modulator into one
# carrier, both at the note's own frequency, with an index that starts
# high and is most of the way down again within a tenth of a second.
# At the top of that envelope the pair is a stack of sidebands on every
# harmonic -- a buzz; at the bottom it is a sine. Struck and then gone,
# which is a bass guitar's string being plucked, and the reason the
# sound sat under half the records of the decade without ever being a
# bass guitar.
#
# 1:1 AND NOTHING ELSE, because every sideband a 1:1 pair makes lands on
# a harmonic of the note: the buzz is bright and still exactly in tune.
# `Ratio' is here because a 2 is the same instrument an octave more
# hollow and a 1.5 is the growl a fifth away, but the preset is 1.
#
# MONOPHONIC, with the slide that implies. `mono = 1' retunes the
# sounding voice rather than starting a second one, so overlapping notes
# slide and separated ones retrigger, and misc::slew on the frequency is
# what makes the slide take time -- the same arrangement as dsp/bass.dsp,
# where the rule is written out at length. A DX bass part is played that
# way because the preset was, and because a line that slides is a line
# with a player in it.
#
# `poly = 2' is one voice sounding and one finishing, so a retrigger
# does not cut the last note's release off where it stood.

name "FM Bass";
author "Misha Nasledov";
description "A 1:1 operator pair with a fast index decay: the DX bass, monophonic and sliding.";
category "Bass";

    @index = 6;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 14;
    @index.label = "Index";

    @id = 70 ms;
    @id.widget = 1;
    @id.min = 5ms;
    @id.max = 1000ms;
    @id.label = "Index Decay";

    @is = 0.12;
    @is.widget = 1;
    @is.min = 0;
    @is.max = 1;
    @is.label = "Index Floor";

    @ratio = 1;
    @ratio.widget = 1;
    @ratio.min = 0.5;
    @ratio.max = 4;
    @ratio.label = "Modulator Ratio";

    # The modulator's own output back into its phase, which fills in the
    # partials between the ones a sine modulator reaches. A quarter of a
    # radian is a rounder pick; the top of the slider is a fuzz box.
    @grit = 0.15;
    @grit.widget = 1;
    @grit.min = 0;
    @grit.max = 1;
    @grit.label = "Growl";

    @glide = 45 ms;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 500ms;
    @glide.label = "Glide";

    @a = 3 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 200ms;
    @a.label = "Attack";

    @d = 400 ms;
    @d.widget = 1;
    @d.min = 20ms;
    @d.max = 4000ms;
    @d.label = "Decay";

    # The bottom of the slider is a little above zero on purpose: an
    # env::adsr whose decay arrives at a sustain of exactly nothing ends
    # the note, and a still-held key then reads as a retrigger and starts
    # it again. Everything above this is an envelope; zero is a loop.
    @s = 0.55;
    @s.widget = 1;
    @s.min = 0.02;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 90 ms;
    @r.widget = 1;
    @r.min = 5ms;
    @r.max = 2000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    mono = 1;
    poly = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The slide. Its state belongs to the voice, so it carries across a
# retune; with `Glide' at zero, mono is a hard retune, which is also a
# sound a DX bass line is played with.
node glide misc::slew {
    in = freq->out;
    time = @glide;
};

# The pluck. env::ad and not an ADSR: an env::adsr that finishes its
# decay with `s = 0' ends the note, sees the trigger still held, and
# starts again, which on an index is a loop rather than an envelope. The
# floor a held note settles on is arithmetic on the carrier's index
# instead -- `Index Floor' of the way up, and the rest of the way from
# here.
node idx env::ad {
    a = 0;
    d = @id;
};

node modop osc::fmop {
    freq = glide->out;
    ratio = @ratio;
    feedback = @grit;
};

# Velocity on the index and not on the gain: a harder note is a brighter
# pluck, which is the accent a bass line is written with.
node carrier osc::fmop {
    freq = glide->out;
    ratio = 1;
    mod = modop->out;
    index = (@is + (1 - @is) * idx->out) * @index * ionode->velocity;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * (0.6 + ionode->velocity * 0.4);
    r = @r;
    p = 0.6 + ionode->velocity * 0.4;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = carrier->out * 0.8;
    in1 = env->out;
};

io ionode;
