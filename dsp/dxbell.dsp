# FM Bell -- one pair at 1:3.5, and the half is the whole trick.
#
# A modulator at three and a half times the carrier puts its sidebands
# at 2.5, 4.5, 6, 8 times the note and so on -- none of them a harmonic,
# because 3.5 and 1 have no common measure worth the name. A partial
# that is not a harmonic does not fuse into the note the way a harmonic
# does; the ear hears it as a separate ring hanging over the pitch, and
# a stack of them is a bell, a tubular chime or a marimba depending only
# on how fast they decay. Nothing else in the tree does this: a filter
# moves harmonics around and cannot invent an inharmonic one, and an
# additive oscillator can place them but not make them move.
#
# TWO DECAYS, AND THE INDEX GOES FIRST. `Ring Decay' takes the
# index down over a second or two, so the inharmonic partials fade while
# the note is still sounding; `Decay' takes the amplitude down over
# several, so what is left at the end is the carrier alone, a sine, the
# hum a bell settles into. Reversing them -- the index outlasting the
# amplitude -- is a marimba, and is one slider.
#
# NO SUSTAIN AND NO KEY RELEASE. The amplitude envelope has no sustain
# and is not wired to the key at all: it runs its decay once and ends
# the note there, whatever the player did, which is what a struck thing
# does -- a bell that stopped when you let go would be a bell with a
# hand on it. `trigger = 0' is load-bearing and not laziness. An
# env::adsr that reaches the end of a decay with `s = 0' ends the note,
# and then reads the trigger, and a held key reads as a retrigger: the
# bell would start again every `Decay' for as long as the note was
# held. With nothing on the trigger there is nothing to restart it.
#
# `Ratio' at a whole number is not a bell. 3 is an organ pipe, 2 is a
# clarinet, and 3.5, 4.7 and 11.1 are all bells of different sizes --
# the further from a simple fraction, the more metal.

name "FM Bell";
author "Misha Nasledov";
description "An inharmonic operator pair, index decaying under a longer amplitude: the DX bell.";

    @ratio = 3.5;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 14;
    @ratio.label = "Ratio";

    @index = 5;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 14;
    @index.label = "Index";

    @id = 1400 ms;
    @id.widget = 1;
    @id.min = 50ms;
    @id.max = 8000ms;
    @id.label = "Ring Decay";

    # What is left of the ring once `Ring Decay' has run out. At zero the
    # bell ends as a pure sine; a little of it keeps some metal in the
    # tail.
    @is = 0.08;
    @is.widget = 1;
    @is.min = 0;
    @is.max = 1;
    @is.label = "Ring Floor";

    # A strike is a hammer hitting metal, so the index arrives with the
    # note. A few milliseconds of attack is a soft beater.
    @ia = 1 ms;
    @ia.widget = 1;
    @ia.min = 0;
    @ia.max = 200ms;
    @ia.label = "Strike";

    @d = 4500 ms;
    @d.widget = 1;
    @d.min = 100ms;
    @d.max = 16000ms;
    @d.label = "Decay";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# env::ad: one strike, one decay, and nothing that restarts while the
# key is held. (An env::adsr whose decay ends at `s = 0' ends the note
# and is then retriggered by the trigger still being held, which on an
# index is a loop.) `Ring Floor' is arithmetic on the index below rather
# than a sustain here.
node idx env::ad {
    a = @ia;
    d = @id;
};

node modop osc::fmop {
    freq = freq->out;
    ratio = @ratio;
};

# Velocity into the index: a bell struck harder is a bell with more
# metal in it, not a louder sine.
node carrier osc::fmop {
    freq = freq->out;
    ratio = 1;
    mod = modop->out;
    index = (@is + (1 - @is) * idx->out) * @index * ionode->velocity;
};

# The note is over when the decay is, key or no key -- see the head for
# why the trigger is a constant.
node env env::adsr {
    a = 0;
    d = @d;
    s = 0;
    r = 0;
    p = 0.55 + ionode->velocity * 0.45;
    trigger = 0;
};

node vca mixer::mul {
    in0 = carrier->out * 0.8;
    in1 = env->out;
};

io ionode;
