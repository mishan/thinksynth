# Wah -- a band-pass with something moving it.
#
# The pedal is a band-pass filter on a treadle, and everything that
# makes it sound like a voice is in the resonance: a narrow band swept
# across a signal picks out one formant after another, which is what a
# vowel is. So `Focus' here is high by default, and turning it down
# gives a tone control rather than a wah.
#
# TWO WAYS TO MOVE IT, and `Auto' crossfades between them rather than
# switching, because there is no reason a piece cannot have both.
#
#   THE ENVELOPE. `env::follower' on what the channel is playing: the
#   filter opens on a transient and falls back as the note decays, which
#   on a guitar's sixteenths is a note-by-note vowel and is the funk
#   pedal nobody's foot could keep up with. `Speed' is how fast it falls
#   back -- fast is a quack on every note, slow is one long sweep over a
#   phrase.
#
#   THE PEDAL. `Pedal' is a chanarg, so a `gen::walk' or a `gen::pump'
#   on the channel rocks it from the composer side, which is the treadle
#   played by something that does not get tired. At `Auto = 0' that is
#   the whole of the movement.
#
# The band is taken from the same filter on both sides at the same
# cutoff: a wah is a mono pedal in front of an amp, and two of them
# moving independently is a phaser.

name "Wah";
author "Misha Nasledov";
description "A resonant band-pass swept by the signal's own envelope or by a pedal.";
category "Effects";

    @low = 420;
    @low.widget = 1;
    @low.min = 100;
    @low.max = 2000;
    @low.label = "Heel (Hz)";

    @high = 2400;
    @high.widget = 1;
    @high.min = 300;
    @high.max = 8000;
    @high.label = "Toe (Hz)";

    @res = 0.9;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Focus";

    @auto = 1;
    @auto.widget = 1;
    @auto.min = 0;
    @auto.max = 1;
    @auto.label = "Auto";

    @speed = 2.2;
    @speed.widget = 1;
    @speed.min = 0.5;
    @speed.max = 6;
    @speed.label = "Speed";

    @pedal = 0.3;
    @pedal.widget = 1;
    @pedal.min = 0;
    @pedal.max = 1;
    @pedal.label = "Pedal";

    @mix = 0.85;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# What the channel is doing, in one number. The sum of the two sides,
# because the pedal is in front of the amp and hears all of it.
node level env::follower {
    in = (ionode->in0 + ionode->in1) * 0.5;
    falloff = @speed;
};

# Where the treadle is: the follower and the pedal, crossfaded by
# `Auto'. One expression, so a piece that automates `Pedal' while `Auto'
# is up gets both at once rather than an argument.
node foot math::clamp {
    in = level->out * @auto + @pedal * (1 - @auto);
    lo = 0;
    hi = 1;
};

node wahl filt::svf {
    in = ionode->in0;
    cutoff = @low + (@high - @low) * foot->out;
    res = @res;
};

node wahr filt::svf {
    in = ionode->in1;
    cutoff = @low + (@high - @low) * foot->out;
    res = @res;
};

# filt::svf's band output peaks at 1 whatever the resonance is, so a
# narrow one hands back a slice of what it was given and the pedal reads
# as a volume drop. A real wah has ten decibels of gain at its peak, and
# that is what the makeup is: unity at no resonance and three and a
# quarter times at the top of the knob, which is where the band is
# narrow enough to need it.
node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = wahl->out_band * (1 + @res * 2.5);
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = wahr->out_band * (1 + @res * 2.5);
    fade = @mix;
};

io ionode;
