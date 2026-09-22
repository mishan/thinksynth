# Kit Hat -- two cymbals, some air, and a foot.
#
# hat808.dsp is six inharmonic partials and nothing else, which is what
# the circuit was and why it reads as metal. A pair of hi-hats on a
# stand is that plus the sound of air being squeezed between them: the
# partials are the alloy, and the noise through a high-pass is the
# sizzle -- the part that tells the ear something physical is rattling
# rather than a bank of oscillators running.
#
# VELOCITY IS THE PEDAL, as it is on every hat here: a hard stroke is an
# open hat and a soft one is closed, because on a kit they are one
# instrument played with the foot up or down. Squared, so the middle of
# the range does not sit half open.
#
# `Sizzle' is the open hat's other half. The partials decay the same
# either way; what an open hat has and a closed one does not is the air
# between the cymbals, so the noise gets its own decay and the knob that
# sets how far it runs past the metal.
#
# THE PEDAL, `choke = 1'. The open hat on the `and' is cut by the closed
# one that lands on the beat, because a foot cannot be in two places.
# Which voice to end is the channel's knowledge and not a graph's, so
# the engine ends it and `Pedal Close' only says how fast. The engine
# marks a cut voice by taking `trigger' negative, which is what
# `clamp(1 + trigger, 0, 1)' below is reading: 1 for a key down, 1 for a
# key up -- a hat's length is its velocity's business, not the
# sequencer's -- and 0 only for a voice the choke took.

name "Kit Hat";
author "Misha Nasledov";
description "Inharmonic partials with air between them, velocity opening the hat and the pedal closing it.";
category "Drums";

    @freq = 760;
    @freq.widget = 1;
    @freq.min = 200;
    @freq.max = 3000;
    @freq.label = "Tune (Hz)";

    @pmul = 1.41;
    @pmul.widget = 1;
    @pmul.min = 1;
    @pmul.max = 3;
    @pmul.label = "Partial Ratio";

    @spread = 290;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 900;
    @spread.label = "Spread (Hz)";

    @tone = 8200;
    @tone.widget = 1;
    @tone.min = 2000;
    @tone.max = 16000;
    @tone.label = "Edge (Hz)";

    @air = 0.55;
    @air.widget = 1;
    @air.min = 0;
    @air.max = 1.5;
    @air.label = "Air";

    @sizzle = 1.6;
    @sizzle.widget = 1;
    @sizzle.min = 0.2;
    @sizzle.max = 4;
    @sizzle.label = "Sizzle";

    @clen = 55 ms;
    @clen.widget = 1;
    @clen.min = 10ms;
    @clen.max = 500ms;
    @clen.label = "Closed";

    @olen = 480 ms;
    @olen.widget = 1;
    @olen.min = 20ms;
    @olen.max = 3000ms;
    @olen.label = "Open";

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

# Squared, so the pedal is up for the hits a pattern accents and down
# for everything else, rather than half open across the middle.
node how math::mul {
    in0 = ionode->velocity;
    in1 = ionode->velocity;
};

node metal osc::multiwave {
    waves = 6;
    freq = @freq;
    amp = 0.9;
    pitchmul = @pmul;
    pitchadd = @spread;
    ampmul = 0.8;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The alloy and the air, filtered together: the band is the body of a
# cymbal and what is above it is the sound of the two of them meeting.
node edge filt::svf {
    in = metal->out * 0.4 + noise->out * @air * 0.35;
    cutoff = @tone;
    res = 0.25;
};

node len mixer::fade {
    in0 = @clen;
    in1 = @olen;
    fade = how->out;
};

node env env::ad {
    a = 0.2 ms;
    d = len->out;
    p = ionode->velocity;
};

# The air outlasts the metal on an open hat and not on a closed one,
# which is what `Sizzle' says: the same envelope stretched, and only for
# the part of the hit that is noise.
node aenv env::ad {
    a = 0.2 ms;
    d = @clen + (@olen * @sizzle - @clen) * how->out;
    p = ionode->velocity;
};

node foot env::adsr {
    a = 0;
    d = 0;
    s = th_max;
    r = @pedal;
    trigger = clamp(1 + ionode->trigger, 0, 1);
};

node out math::mul {
    in0 = edge->out_band * 0.8 * env->out +
          edge->out_high * 0.9 * aenv->out;
    in1 = foot->out;
};

io ionode;
