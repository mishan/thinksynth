# Spring -- the reverb in a guitar amp and on every dub record.
#
# A spring is a delay line that cannot keep the frequencies together: a
# wave in steel travels at a speed that depends on its pitch, so a click
# put in one end comes out the other as a chirp, and every trip back and
# forth along the coil smears it further. That chirp, repeating every few
# tens of milliseconds and dying, is the drip and the boing.
#
# THE CHIRP is first-order allpasses in series: each passes every
# frequency at full level but delays the ones under `Chirp' more than the
# ones over it, about a millisecond each at the default. Twelve go on
# before the spring, so a click's bottom arrives some ten milliseconds
# after its top, and four more on every trip round, so each repeat is
# smeared further than the last, as in steel. THE REPEATS are a delay at
# the spring's length with that chirp and a low-pass and a high-pass in
# its loop, darker and thinner each time. Two springs of different
# lengths, one a side, as the tanks in an amplifier have.
#
# The nodes on the loop run a sample at a time, several times the cost
# of a window at once, so the first twelve allpasses are kept off it.

name "Spring";
author "Misha Nasledov";
description "A spring reverb: the chirp of a dispersive line, repeating every few tens of milliseconds, darker and thinner each time.";
category "Effects";

    @chirp = 350;
    @chirp.widget = 1;
    @chirp.min = 100;
    @chirp.max = 2000;
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

# The chirp, then a short diffuser so a repeat is a smear and not a
# copy: the drip. Then the spring, which reads what came round it a
# sample ago through four more allpasses and the two filters.
node apl0 filt::allpass { in = ionode->in0; freq = @chirp; };
node apl1 filt::allpass { in = apl0->out; freq = @chirp; };
node apl2 filt::allpass { in = apl1->out; freq = @chirp; };
node apl3 filt::allpass { in = apl2->out; freq = @chirp; };
node apl4 filt::allpass { in = apl3->out; freq = @chirp; };
node apl5 filt::allpass { in = apl4->out; freq = @chirp; };
node apl6 filt::allpass { in = apl5->out; freq = @chirp; };
node apl7 filt::allpass { in = apl6->out; freq = @chirp; };
node apl8 filt::allpass { in = apl7->out; freq = @chirp; };
node apl9 filt::allpass { in = apl8->out; freq = @chirp; };
node apl10 filt::allpass { in = apl9->out; freq = @chirp; };
node apl11 filt::allpass { in = apl10->out; freq = @chirp; };

node dripl delay::allpass { in = apl11->out; delay = 3.1 ms; gain = 0.6; };

node springl delay::echo {
    in = dripl->out + lowl->out_high * min(@decay, 0.95);
    size = 100 ms;
    delay = 33 ms;
    feedback = 0;
    dry = 0;
};

node lapl0 filt::allpass { in = springl->out; freq = @chirp; };
node lapl1 filt::allpass { in = lapl0->out; freq = @chirp; };
node lapl2 filt::allpass { in = lapl1->out; freq = @chirp; };
node lapl3 filt::allpass { in = lapl2->out; freq = @chirp; };
node tonel filt::svf { in = lapl3->out; cutoff = @tone; res = 0; };
node lowl filt::svf { in = tonel->out_low; cutoff = @low; res = 0; };

node apr0 filt::allpass { in = ionode->in1; freq = @chirp; };
node apr1 filt::allpass { in = apr0->out; freq = @chirp; };
node apr2 filt::allpass { in = apr1->out; freq = @chirp; };
node apr3 filt::allpass { in = apr2->out; freq = @chirp; };
node apr4 filt::allpass { in = apr3->out; freq = @chirp; };
node apr5 filt::allpass { in = apr4->out; freq = @chirp; };
node apr6 filt::allpass { in = apr5->out; freq = @chirp; };
node apr7 filt::allpass { in = apr6->out; freq = @chirp; };
node apr8 filt::allpass { in = apr7->out; freq = @chirp; };
node apr9 filt::allpass { in = apr8->out; freq = @chirp; };
node apr10 filt::allpass { in = apr9->out; freq = @chirp; };
node apr11 filt::allpass { in = apr10->out; freq = @chirp; };

node dripr delay::allpass { in = apr11->out; delay = 3.7 ms; gain = 0.6; };

node springr delay::echo {
    in = dripr->out + lowr->out_high * min(@decay, 0.95);
    size = 100 ms;
    delay = 41 ms;
    feedback = 0;
    dry = 0;
};

node lapr0 filt::allpass { in = springr->out; freq = @chirp; };
node lapr1 filt::allpass { in = lapr0->out; freq = @chirp; };
node lapr2 filt::allpass { in = lapr1->out; freq = @chirp; };
node lapr3 filt::allpass { in = lapr2->out; freq = @chirp; };
node toner filt::svf { in = lapr3->out; cutoff = @tone; res = 0; };
node lowr filt::svf { in = toner->out_low; cutoff = @low; res = 0; };

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
