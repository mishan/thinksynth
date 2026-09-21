# Waveguide -- a burst of noise shut in a tube the length of the note.
#
# The oldest trick in physical modelling and still the cheapest: excite a
# delay line one wavelength long, feed its output back into itself
# slightly quieter and slightly darker, and what comes out is a plucked
# string. The pitch is the length of the line and nothing else -- there
# is no oscillator in this file.
#
# `filt::comb' is the whole instrument. It takes its delay as a
# frequency, so the note's own pitch goes straight in; `feedback' is how
# much of each pass survives, and `damp' is how much darker each one is
# than the last. Its own description says it: 0 is a tube and up is a
# string. That is the difference between a flute and a guitar, in one
# number, and it is physical rather than decorative -- a real string
# loses its high partials to the bridge faster than its low ones, which
# is why a plucked note starts bright and ends round.
#
# THE LOOP IS INSIDE THE PLUGIN, and that is the point. The obvious way
# to build this is a `delay::echo' whose output is filtered, saturated
# and wired back to its own input, which is a cycle in the graph.
# DSP_FORMAT.md says what happens to one: the walk clears each node's
# recalc flag before it recurses, so the loop closes through a whole
# window and the file sounds like whatever buffer size the audio device
# asked for. `dsp/noargs/smoothie.dsp' was written that way in 2004 and
# is where this instrument comes from; the delay line it wanted is the
# one `filt::comb' already has.
#
# THE EXCITATION IS NOISE AND NOT AN OSCILLATOR. A comb at 110 Hz only
# rings at the partials of 110 Hz, so what it is fed decides the timbre
# and not the pitch: anything broadband will do, and noise is broadband
# by definition. `Pluck' is how long the burst lasts. Short is a
# fingernail, long is a bow, and past about 40 ms it stops being a pluck
# and starts being the sound of somebody sawing.
#
# `Pick' is a low-pass on the burst before it goes in. It sets which
# partials get excited at all, which is where on the string it was
# plucked -- near the bridge is bright and thin, over the hole is round.
# Nothing after the comb can put back a partial the pick never gave it.
#
# `Body' is `dist::saturate' after the line and not inside it. Inside is
# where a real instrument's nonlinearity lives and inside is the cycle
# this file exists to avoid; outside, it is the same waveshaper working
# on the same signal one pass later, which thickens the note without
# deciding its pitch.
#
# THE AMP ENVELOPE IS NEARLY OPEN. The decay that matters is the comb's,
# not the envelope's -- `Decay' at 0.99 rings for seconds and at 0.8 is
# a muted stab. The envelope is here so that a note-off stops the string
# the way a hand on it does, which is what `Release' is.

name "Waveguide";
author "Misha Nasledov";
description "A noise burst shut in a comb one wavelength long: the plucked string.";

    @pluck = 6 ms;
    @pluck.widget = 1;
    @pluck.min = 0.5 ms;
    @pluck.max = 80 ms;
    @pluck.label = "Pluck";

    @pick = 3000;
    @pick.widget = 1;
    @pick.min = 300;
    @pick.max = 12000;
    @pick.label = "Pick (Hz)";

    @decay = 0.97;
    @decay.widget = 1;
    @decay.min = 0.7;
    @decay.max = 0.999;
    @decay.label = "Decay";

    @damp = 0.35;
    @damp.widget = 1;
    @damp.min = 0;
    @damp.max = 0.95;
    @damp.label = "Damping";

    @body = 1.4;
    @body.widget = 1;
    @body.min = 1;
    @body.max = 6;
    @body.label = "Body";

    @r = 120 ms;
    @r.widget = 1;
    @r.min = 2 ms;
    @r.max = 2000 ms;
    @r.label = "Release";

    @level = 1.5;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    out0 = vca->out;
    out1 = vca->out;
    channels = 2;
    play = amp->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The burst. `env::ad' and not an ADSR: a pluck is over before anybody
# lets go of the key, and an envelope that waits for a note-off would
# excite the line for as long as the note was held.
node burst env::ad {
    a = 0;
    d = @pluck;
    p = ionode->velocity;
};

node exciter osc::noise {
    color = 0;
    amp = 1;
};

node pick filt::svf {
    in = exciter->out * burst->out;
    cutoff = @pick;
    res = 0;
};

# The string. `size' is a constant on purpose -- filt::comb reads it once
# a window and clears the line when it changes, so a modulated one would
# silence the note every time it moved. Half a second is room for a
# wavelength down to 2 Hz, which is four octaves below anything anybody
# plays.
node string filt::comb {
    in = pick->out_low;
    freq = freq->out;
    feedback = @decay;
    damp = @damp;
    size = 500 ms;
};

node body dist::saturate {
    in = string->out;
    factor = @body;
};

# Almost entirely open: the note's shape is the comb's decay, and this
# is only here to let go of it.
node amp env::adsr {
    a = 1 ms;
    d = 1 ms;
    s = 100%;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = body->out * @level;
    in1 = amp->out;
};

io ionode;
