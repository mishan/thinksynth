# Hat 808 -- six partials that share no fundamental.
#
# What an 808 hi-hat actually is: six square-wave oscillators at
# frequencies picked so that none of them is a harmonic of any other,
# summed, and sent through a band-pass and a high-pass. There is no
# noise anywhere in it. That is the trick, and it is why an 808 hat
# sounds like an 808 hat and not like a filtered hiss: six inharmonic
# tones are dense enough to read as metal and sparse enough that the
# ear can still hear their beating.
#
# `osc::multiwave' sums sines in a series with two knobs on the spacing:
# `pitchmul' multiplies each partial by the last and `pitchadd' adds a
# fixed number of hertz per partial. The add is the whole reason this
# graph is possible -- a series that is only multiplied is harmonic
# however you set it, and it is the offset that breaks the ratios. With
# `Spread' at a few hundred hertz the six partials share no common
# factor worth the name, which is as close to the original's six
# oscillators as a series can get.
#
# Sines and not squares, because the band-pass above 8 kHz is throwing
# away everything a square would have added anyway.
#
# VELOCITY IS THE PEDAL, not the loudness -- `hat0.dsp' does the same
# and for the same reason. On a kit the two hi-hat sounds are one
# instrument played with the foot up or down, so a hard hit is an open
# hat and a soft one is closed, and the velocity curve is squared to
# keep the middle of the range from sitting between the two. A pattern
# that wants dynamics instead of a pedal drives the channel's amplitude.
#
# And with `choke = 1' the pedal is a pedal: the open hat on the `and' is
# cut by the closed one that lands on the beat, because a foot cannot be
# in two places. Which voice to end is the channel's knowledge and not a
# graph's, so the engine ends it and `Pedal Close' only says how fast.

name "Hat 808";
author "Misha Nasledov";
description "Six inharmonic partials through a band-pass, velocity opening the hat: the 808 hi-hat.";

    @freq = 800;
    @freq.widget = 1;
    @freq.min = 200;
    @freq.max = 3000;
    @freq.label = "Tune (Hz)";

    # Each partial is the last one times this...
    @pmul = 1.47;
    @pmul.widget = 1;
    @pmul.min = 1;
    @pmul.max = 3;
    @pmul.label = "Ratio";

    # ...plus this many hertz, which is what stops the series being
    # harmonic at any ratio at all.
    @spread = 320;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 2000;
    @spread.label = "Spread (Hz)";

    @tone = 9000;
    @tone.widget = 1;
    @tone.min = 2000;
    @tone.max = 16000;
    @tone.label = "Band (Hz)";

    @res = 0.3;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Band Width";

    @clen = 60 ms;
    @clen.widget = 1;
    @clen.min = 10ms;
    @clen.max = 1000ms;
    @clen.label = "Closed Decay";

    @olen = 420 ms;
    @olen.widget = 1;
    @olen.min = 10ms;
    @olen.max = 3000ms;
    @olen.label = "Open Decay";

    @pedal = 12 ms;
    @pedal.widget = 1;
    @pedal.min = 1ms;
    @pedal.max = 4000ms;
    @pedal.label = "Pedal Close";

node ionode {
    channels = 2;

    # One hat sounding and one being cut: `choke' is what makes the pair
    # and `poly' is the room their overlap needs.
    choke = 1;
    poly = 2;

    out0 = out->out;
    out1 = out->out;
    play = env->play * foot->out;
};

node metal osc::multiwave {
    waves = 6;
    freq = @freq;
    amp = 0.9;
    pitchmul = @pmul;
    pitchadd = @spread;
    ampmul = 0.82;
};

node band filt::svf {
    in = metal->out * 0.4;
    cutoff = @tone;
    res = @res;
};

# Squared, so the pedal is up for the hits a pattern accents and down
# for everything else, rather than half open across the middle.
node pedal math::mul {
    in0 = ionode->velocity;
    in1 = ionode->velocity;
};

node decay mixer::fade {
    in0 = @clen;
    in1 = @olen;
    fade = pedal->out;
};

node env env::ad {
    a = 0.2 ms;
    d = decay->out;
};

# The high output over the band: the band is the body of the hat and
# what is above it is the air, and a hat with no air is a woodblock.
# The pedal. `choke = 1' on the io node sends every voice that is still
# sounding into its release when the next hat lands, and marks it by
# taking `trigger' negative -- so this is 1 for a key down, 1 for a key
# up, and 0 only for a voice the choke took. A note-off therefore does
# not shorten a hat, which is what a hat wants: its length is in the
# velocity. `play' is multiplied by it too, so a choked voice retires as
# soon as it is quiet rather than sitting out the rest of its decay.
#
# A `Pedal Close' at the top of its range is a hat that ignores the
# pedal: the release outlasts the hat's own envelope, so nothing is ever
# cut, which is the sound this graph had before it could be.
node foot env::adsr {
    a = 0;
    d = 0;
    s = th_max;
    r = @pedal;
    trigger = clamp(1 + ionode->trigger, 0, 1);
};

node out mixer::mul {
    in0 = band->out_band * 0.7 + band->out_high * 0.5;
    in1 = env->out * foot->out;
};

io ionode;
