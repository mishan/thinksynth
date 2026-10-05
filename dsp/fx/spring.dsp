# Spring -- the reverb in a guitar amp and on every dub record.
#
# A spring is a delay line that cannot keep the frequencies together: a
# wave in steel travels at a speed that depends on its pitch, so a click
# put in one end comes out the other as a chirp, and every trip back and
# forth along the coil smears it further. That chirp, repeating every few
# tens of milliseconds and dying, is the drip and the boing.
#
# THE CHIRP is eight first-order allpasses in series: each passes every
# frequency at full level but delays the ones under `Chirp' more than
# the ones over it, and eight of them in a row are a sweep long enough
# to hear. THE REPEATS are delay::echo at the spring's length, darkened
# and thinned on every lap the way a spring loses its top and its
# bottom. Two springs of different lengths, one a side, as the tanks in
# an amplifier have.
#
# The chirp is put on once, before the repeats rather than on each lap:
# a graph cannot run a filter inside a delay's loop, and the dispersion
# that builds lap by lap in steel is the one thing traded away.

name "Spring";
author "Misha Nasledov";
description "A spring reverb: the chirp of a dispersive line, repeating every few tens of milliseconds, darker and thinner each time.";
category "Effects";

    @chirp = 900;
    @chirp.widget = 1;
    @chirp.min = 200;
    @chirp.max = 4000;
    @chirp.label = "Chirp (Hz)";

    @decay = 0.75;
    @decay.widget = 1;
    @decay.min = 0;
    @decay.max = 0.95;
    @decay.label = "Decay";

    @tone = 4500;
    @tone.widget = 1;
    @tone.min = 1000;
    @tone.max = 10000;
    @tone.label = "Tone (Hz)";

    @low = 180;
    @low.widget = 1;
    @low.min = 20;
    @low.max = 800;
    @low.label = "Low Cut (Hz)";

    @mix = 0.3;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

node apl0 filt::allpass { in = ionode->in0; freq = @chirp; };
node apl1 filt::allpass { in = apl0->out; freq = @chirp; };
node apl2 filt::allpass { in = apl1->out; freq = @chirp; };
node apl3 filt::allpass { in = apl2->out; freq = @chirp; };
node apl4 filt::allpass { in = apl3->out; freq = @chirp; };
node apl5 filt::allpass { in = apl4->out; freq = @chirp; };
node apl6 filt::allpass { in = apl5->out; freq = @chirp; };
node apl7 filt::allpass { in = apl6->out; freq = @chirp; };

node apr0 filt::allpass { in = ionode->in1; freq = @chirp; };
node apr1 filt::allpass { in = apr0->out; freq = @chirp; };
node apr2 filt::allpass { in = apr1->out; freq = @chirp; };
node apr3 filt::allpass { in = apr2->out; freq = @chirp; };
node apr4 filt::allpass { in = apr3->out; freq = @chirp; };
node apr5 filt::allpass { in = apr4->out; freq = @chirp; };
node apr6 filt::allpass { in = apr5->out; freq = @chirp; };
node apr7 filt::allpass { in = apr6->out; freq = @chirp; };

# A short diffuser after the chirp, so a repeat is a smear and not a
# copy: the drip.
node dripl delay::allpass { in = apl7->out; delay = 3.1 ms; gain = 0.6; };
node dripr delay::allpass { in = apr7->out; delay = 3.7 ms; gain = 0.6; };

# The input scaled up by what delay::echo's crossfade takes off it; see
# fx/dub.dsp.
node springl delay::echo {
    in = dripl->out / (1 - @decay);
    size = 100 ms;
    delay = 33 ms;
    feedback = @decay;
    dry = 0;
    tone = @tone;
    low = @low;
};

node springr delay::echo {
    in = dripr->out / (1 - @decay);
    size = 100 ms;
    delay = 41 ms;
    feedback = @decay;
    dry = 0;
    tone = @tone;
    low = @low;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = springl->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = springr->out;
    fade = @mix;
};

io ionode;
