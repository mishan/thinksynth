# Echo -- a delay on a channel's sum, which is what a throw needs.
#
# An echo inside an instrument dies with the note that fed it: the ring
# it lives in is part of the voice, and the voice is gone a beat after
# the key came up. This graph is not anybody's note. The engine hands it
# whatever the channel's voices summed to -- `in0' and `in1' on the io
# node, written the way `note' and `velocity' are written into a voice --
# and reads `out0' and `out1' back, every window, whether or not anything
# is playing. The tail is the part that plays when nothing is.
#
# The dry signal is the graph's business and not the engine's: `in0' goes
# into the mix beside the wet, so `Mix' is a knob here rather than a
# convention somewhere in C++.
#
# `Damping' is a lowpass on the wet, which is what stops a long tail
# turning into a hall of mirrors. On the wet as a whole rather than
# inside the feedback path: putting it there would mean the ring reading
# the filter and the filter reading the ring, and a graph with a cycle in
# it resolves as a one-window delay -- so what it sounded like would
# depend on the window length, which is the one thing a .dsp may not do.
# The repeats therefore darken together rather than one after another.

name "Echo";
author "Misha Nasledov";
description "A stereo delay for a channel, with a damped tail.";

    @delay = 375 ms;
    @delay.widget = 1;
    @delay.min = 10ms;
    @delay.max = 2000ms;
    @delay.label = "Delay";

    @spread = 1.5;
    @spread.widget = 1;
    @spread.min = 0.25;
    @spread.max = 4;
    @spread.label = "Right Delay x";

    @feedback = 0.45;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 0.95;
    @feedback.label = "Feedback";

    @damping = 3200;
    @damping.widget = 1;
    @damping.min = 400;
    @damping.max = 16000;
    @damping.label = "Damping (Hz)";

    @mix = 0.3;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    # The engine writes these every window. A file declares them so the
    # nodes below have something to read and the editor has a port to
    # draw; whatever a file puts here is overwritten.
    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# One ring per side, each long enough for the longest `delay' the
# control offers, times the widest `spread'. The right side is offset
# so the two do not arrive together -- one expression rather than a
# math::mul node.
node echol delay::echo {
    in = ionode->in0;
    size = 8000 ms;
    delay = @delay;
    feedback = @feedback;
    dry = 0;
};

node echor delay::echo {
    in = ionode->in1;
    size = 8000 ms;
    delay = @delay * @spread;
    feedback = @feedback;
    dry = 0;
};

node dampl filt::svf {
    in = echol->out;
    cutoff = @damping;
    res = 0;
};

node dampr filt::svf {
    in = echor->out;
    cutoff = @damping;
    res = 0;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = dampl->out_low;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = dampr->out_low;
    fade = @mix;
};

io ionode;
