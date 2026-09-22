# Tape Loop -- two machines, one recording and one playing back, and a
# loop of tape many seconds long between them.
#
# fx/tape.dsp is a tape echo: a short loop, a few repeats, each quieter.
# This is the other arrangement, the one Discreet Music is made on: the
# loop is ten or thirty seconds long, what comes off the playback head
# goes back to the record head nearly whole, and everything played into
# it is still there minutes later, layered under what is played next.
#
# THE LOOP IS AN ADDER, NOT A CROSSFADE. delay::echo writes
# `feedback * tap + (1 - feedback) * in', so at 0.95 only a twentieth of
# a new note gets onto the tape. Dividing the input by 1 - feedback first
# turns that into `feedback * tap + in': every note goes on at full
# level, and `Repeats' is only how much of each lap survives the next.
# It is held to 0.98, where a lap keeps 98% and a note is still audible
# after a hundred and fifty of them.
#
# THE LEVEL. Repeats that do not line up in phase add in power, so a
# steady input comes back 1 / sqrt(1 - feedback^2) times as loud; the wet
# is trimmed by the inverse, which is 0.2 at 0.98 -- the first lap of a
# note is quiet and the tape thickens as they pile up, which is the
# point of the thing. `Drive' is a tanh on the wet, scaled back down by
# itself so a quiet loop comes through at unity and a loud one flattens.
#
# DAMPING, TWICE, AND NEITHER IN THE LOOP. A node reading its own output
# is a cycle, which resolves as a one-window delay and would make the
# sound depend on the buffer size; fx/tape.dsp says so at length. So
# `Tone' is a low-pass on what goes onto the tape and the same again on
# what comes off it: heavy, and the same for every lap.
#
# THE TWO SIDES are two loops, the right `Right Loop x' times the left's
# length, so a phrase comes back on one side and then the other and the
# two drift apart for as long as the piece lasts. `Wow' is the capstan,
# a slow wobble on each playback head.
#
# The ring is sixty seconds, which is what `Loop' and `Right Loop x'
# together may reach. It is allocated when the effect's first window
# runs: 2.6 million floats a side at 44.1 kHz.

name "Tape Loop";
author "Misha Nasledov";
description "A loop of tape ten to thirty seconds long that keeps nearly everything played into it: Discreet Music's second machine.";
category "Effects";

    @delay = 12000 ms;
    @delay.widget = 1;
    @delay.min = 1000ms;
    @delay.max = 30000ms;
    @delay.label = "Loop";

    @spread = 1.3;
    @spread.widget = 1;
    @spread.min = 1;
    @spread.max = 2;
    @spread.label = "Right Loop x";

    @feedback = 0.9;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 0.98;
    @feedback.label = "Repeats";

    @wow = 6;
    @wow.widget = 1;
    @wow.min = 0;
    @wow.max = 30;
    @wow.label = "Wow (samples)";

    @rate = 0.25;
    @rate.widget = 1;
    @rate.min = 0.05;
    @rate.max = 2;
    @rate.label = "Wow Rate (Hz)";

    @drive = 1.5;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

    @tone = 2800;
    @tone.widget = 1;
    @tone.min = 400;
    @tone.max = 12000;
    @tone.label = "Tone (Hz)";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = ionode->in0 * (1 - @mix) + hotl->out / @drive * @mix;
    out1 = ionode->in1 * (1 - @mix) + hotr->out / @drive * @mix;
};

node recl filt::svf {
    in = ionode->in0 / (1 - clamp(@feedback, 0, 0.98));
    cutoff = @tone;
    res = 0;
};

node recr filt::svf {
    in = ionode->in1 / (1 - clamp(@feedback, 0, 0.98));
    cutoff = @tone;
    res = 0;
};

node tapel delay::echo {
    in = recl->out_low;
    size = 60000 ms;
    delay = @delay;
    feedback = clamp(@feedback, 0, 0.98);
    dry = 0;
};

node taper delay::echo {
    in = recr->out_low;
    size = 60000 ms;
    delay = @delay * @spread;
    feedback = clamp(@feedback, 0, 0.98);
    dry = 0;
};

node wowl delay::chorus {
    in = tapel->out;
    rate = @rate;
    depth = @wow;
    delay = 40;
    taps = 1;
    mix = 1;
    phase = 0;
};

node wowr delay::chorus {
    in = taper->out;
    rate = @rate;
    depth = @wow;
    delay = 40;
    taps = 1;
    mix = 1;
    phase = 0.5;
};

node playl filt::svf {
    in = wowl->out;
    cutoff = @tone;
    res = 0;
};

node playr filt::svf {
    in = wowr->out;
    cutoff = @tone;
    res = 0;
};

node hotl dist::saturate {
    in = playl->out_low * pow(1 - pow(clamp(@feedback, 0, 0.98), 2), 0.5);
    factor = @drive;
};

node hotr dist::saturate {
    in = playr->out_low * pow(1 - pow(clamp(@feedback, 0, 0.98), 2), 0.5);
    factor = @drive;
};

io ionode;
