# Gate -- a reverb with its tail cut off where the drum stops.
#
# The snare sound of the decade, and an accident: a reverb return run
# through a noise gate keyed by the drum itself, so the room arrives at
# full size and then vanishes -- no decay, just an edge. On the record
# it was a talkback microphone, a gate and a mistake; here it is
# fx/hall.dsp's comb and allpass section with a gate across the output.
#
# THE KEY IS THE DRY INPUT, WHICH IS THE WHOLE IDEA. Gate the wet signal
# against itself and what you get is a reverb that fades out early --
# the tail closes the gate as it decays, and the closing is as gradual
# as the tail was. Keyed from the dry, the gate knows nothing about the
# tail: it is open for as long as the *drum* is sounding plus `Hold',
# and then it shuts on whatever the room happens to be doing, mid-decay,
# at full level. That cliff is the sound. Nothing else makes it.
#
# `Hold' is the follower's falloff, in the units env::follower declares:
# an exponent, where each whole number is ten times slower -- 3 is about
# twenty milliseconds and 4 about two hundred. It is not in milliseconds
# because it cannot be: the conversion is a logarithm, and the
# expression grammar has `pow', `exp2' and `abs' and no log.
#
# It is the *shut* as much as the hold, which is the thing to know
# before turning it up. A follower is one exponential, so a longer
# falloff holds the gate open longer and also closes it more slowly, and
# past about 3.5 the two stop being distinguishable -- what comes out is
# a reverb fading, which is the sound this effect exists to avoid. At 3,
# measured on a snare, the room holds level for about a hundred and
# forty milliseconds and then falls from a third of full scale to
# nothing in forty. That is the cliff.
#
# `Slam' is what makes it a cliff rather than a slope: the follower is
# multiplied by it before the clamp, so everything above 1/`Slam' of
# full scale reads as simply 1 and only the last sliver of the fall does
# any fading. At 60 the gate is a switch. At 1 it is a fader, and what
# comes out is a reverb with an envelope on it -- softer, less eighties,
# still useful.
#
# Goes on a snare channel. On a whole mix it gates the reverb against
# whatever is loudest, which is a pumping room rather than this effect.

name "Gated Reverb";
author "Misha Nasledov";
description "A comb reverb gated from the dry signal: the eighties snare.";

    @decay = 0.8;
    @decay.widget = 1;
    @decay.min = 0;
    @decay.max = 0.96;
    @decay.label = "Decay";

    @size = 0.7;
    @size.widget = 1;
    @size.min = 0.4;
    @size.max = 2.5;
    @size.label = "Size";

    @damping = 5000;
    @damping.widget = 1;
    @damping.min = 400;
    @damping.max = 16000;
    @damping.label = "Damping (Hz)";

    # See fx/hall.dsp: a unit cannot be written inside arithmetic, so
    # the milliseconds are declared here and scaled below.
    @diffuse = 4 ms;
    @diffuse.widget = 1;
    @diffuse.min = 1 ms;
    @diffuse.max = 25 ms;
    @diffuse.label = "Diffusion";

    @hold = 3;
    @hold.widget = 1;
    @hold.min = 2;
    @hold.max = 4.5;
    @hold.label = "Hold";

    @slam = 60;
    @slam.widget = 1;
    @slam.min = 1;
    @slam.max = 120;
    @slam.label = "Slam";

    @mix = 0.45;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    # The engine writes these every window; a file declares them so the
    # nodes below have something to read. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# The room. fx/hall.dsp's arrangement and its numbers -- four combs a
# side whose delays share no factor, so the echoes do not pile up --
# with `Size' defaulting small, because a gated reverb is a room and not
# a hall and the gate is what makes it big.
node cl0 filt::comb { in = ionode->in0; freq = 33.7 / @size; feedback = @decay; size = 500 ms; };
node cl1 filt::comb { in = ionode->in0; freq = 27.0 / @size; feedback = @decay; size = 500 ms; };
node cl2 filt::comb { in = ionode->in0; freq = 24.3 / @size; feedback = @decay; size = 500 ms; };
node cl3 filt::comb { in = ionode->in0; freq = 22.9 / @size; feedback = @decay; size = 500 ms; };

node cr0 filt::comb { in = ionode->in1; freq = 31.5 / @size; feedback = @decay; size = 500 ms; };
node cr1 filt::comb { in = ionode->in1; freq = 25.2 / @size; feedback = @decay; size = 500 ms; };
node cr2 filt::comb { in = ionode->in1; freq = 22.7 / @size; feedback = @decay; size = 500 ms; };
node cr3 filt::comb { in = ionode->in1; freq = 21.4 / @size; feedback = @decay; size = 500 ms; };

node dampl filt::svf {
    in = (cl0->out + cl1->out + cl2->out + cl3->out) * 0.25;
    cutoff = @damping;
    res = 0;
};

node dampr filt::svf {
    in = (cr0->out + cr1->out + cr2->out + cr3->out) * 0.25;
    cutoff = @damping;
    res = 0;
};

node apl0 delay::allpass { in = dampl->out_low;  delay = @diffuse * @size;        gain = 0.7; };
node apl1 delay::allpass { in = apl0->out;       delay = @diffuse * @size * 0.34; gain = 0.7; };

node apr0 delay::allpass { in = dampr->out_low;  delay = @diffuse * @size * 1.08; gain = 0.7; };
node apr1 delay::allpass { in = apr0->out;       delay = @diffuse * @size * 0.38; gain = 0.7; };

# The key. Both sides summed, because a gate that opened on one channel
# and not the other would put the room in the wrong speaker for as long
# as a drum was panned.
node key env::follower {
    in = ionode->in0 * 0.5 + ionode->in1 * 0.5;
    falloff = @hold;
};

# Driven hard into a clamp, which is what turns a follower into a gate:
# everything above 1/`Slam' of full scale is simply 1, and the shut is
# the follower falling through that last little distance.
node gain math::clamp {
    in = key->out * @slam;
    lo = 0;
    hi = 1;
};

node gatel mixer::mul {
    in0 = apl1->out;
    in1 = gain->out;
};

node gater mixer::mul {
    in0 = apr1->out;
    in1 = gain->out;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = gatel->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = gater->out;
    fade = @mix;
};

io ionode;
