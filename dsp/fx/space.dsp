# Space -- a reverb for a channel whose tail can run for a minute.
#
# delay::fdn, which is eight delay lines mixed through a Hadamard matrix
# rather than fx/hall.dsp's bank of combs. A comb is a resonance, and a
# long comb decay is those resonances standing out of the tail as
# pitches; the network has no loop that is one line's alone, so its
# tail stays noise however long it is asked to last. `Decay' is the
# time the tail takes to fall sixty decibels, in seconds, and sixty is
# allowed.
#
# Around the network, in order: a pre-delay, which keeps an attack in
# front of its own reverb; the network, fed the average of the two
# sides and answering with two uncorrelated taps, which is the stereo;
# a low cut on the wet, so a long tail does not fill the bottom of the
# mix; and a high cut on it, for a tail darker than the damping makes
# it. `Damping' is the network's own low-pass, inside the loop, and it
# is strong -- every pass goes through it again -- so a long bright
# tail wants it high.
#
# The wet is trimmed by sqrt(1 - g^2), for g the network's average gain
# a pass: a steady input comes out of the network 1 / (1 - g^2) times
# as loud in power as it went in, six decibels at two seconds and twenty
# at sixty, and the trim takes exactly that back off. So `Mix' is a
# crossfade between two things of about the same level at any `Decay',
# and turning the decay up makes the tail longer rather than louder. g
# is 10^(-3 L / (decay * rate)) for the lines' average length L, 43 ms
# at size 1, which is where the 0.26 comes from.
#
# delay::echo's delay is how far back its tap reads from the sample it
# is about to write, so zero reads a whole ring ago; the sample added
# to the pre-delay is what makes `Pre-delay = 0' mean none.
#
# `Mix' scales what goes into the network rather than what comes out of
# it, and the dry is added back at 1 - mix: the network and the filters
# are linear, so that is the crossfade it always was, and it leaves room
# for a second input.
#
# That input is the send bus. As a master effect, send0 and send1 carry
# every channel at its instrument's `send', and they go into the network
# at full level beside the mix's own share -- so with `Mix' at zero the
# mix passes dry and each channel is in the room by exactly its send. On
# a channel the send reads zeros and this is the channel reverb.
#
# This is an effect graph -- `in0' on the io node -- and it runs on the
# channel's summed voices every window, which is what lets the tail
# outlive the note. See fx/echo.dsp.

name "Space";
author "Misha Nasledov";
description "A long, smooth reverb for a channel: a feedback delay network, up to a minute of tail.";
category "Effects";

    @predelay = 20 ms;
    @predelay.widget = 1;
    @predelay.min = 0;
    @predelay.max = 500ms;
    @predelay.label = "Pre-delay";

    @decay = 4;
    @decay.widget = 1;
    @decay.min = 0.3;
    @decay.max = 60;
    @decay.label = "Decay (s)";

    @size = 1.5;
    @size.widget = 1;
    @size.min = 0.5;
    @size.max = 3;
    @size.label = "Size";

    @damping = 9000;
    @damping.widget = 1;
    @damping.min = 1000;
    @damping.max = 18000;
    @damping.label = "Damping (Hz)";

    # How far each line's read swings, in samples: what keeps a long
    # tail from settling into a chord. At zero the network is still, and
    # a still network at a long decay is where metallic comes back.
    @motion = 12;
    @motion.widget = 1;
    @motion.min = 0;
    @motion.max = 32;
    @motion.label = "Motion";

    @lowcut = 100;
    @lowcut.widget = 1;
    @lowcut.min = 20;
    @lowcut.max = 1000;
    @lowcut.label = "Low Cut (Hz)";

    @highcut = 12000;
    @highcut.widget = 1;
    @highcut.min = 1000;
    @highcut.max = 18000;
    @highcut.label = "High Cut (Hz)";

    @mix = 0.3;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    send0 = 0;
    send1 = 0;

    out0 = ionode->in0 * (1 - @mix) + highl->out_low;
    out1 = ionode->in1 * (1 - @mix) + highr->out_low;
};

node pre delay::echo {
    in = ((ionode->in0 + ionode->in1) * @mix +
          ionode->send0 + ionode->send1) * 0.5;
    size = 510 ms;
    delay = @predelay + 1;
    feedback = 0;
    dry = 0;
};

node net delay::fdn {
    in = pre->out;
    size = @size;
    decay = @decay;
    damping = @damping;
    mod = @motion;
    rate = 0.3;
    diffuse = 0.7;
};

node lowl filt::svf {
    in = net->out * pow(1 - pow(10, -0.26 * @size / @decay), 0.5);
    cutoff = @lowcut;
    res = 0;
};

node lowr filt::svf {
    in = net->out2 * pow(1 - pow(10, -0.26 * @size / @decay), 0.5);
    cutoff = @lowcut;
    res = 0;
};

node highl filt::svf {
    in = lowl->out_high;
    cutoff = @highcut;
    res = 0;
};

node highr filt::svf {
    in = lowr->out_high;
    cutoff = @highcut;
    res = 0;
};

io ionode;
