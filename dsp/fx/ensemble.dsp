# Ensemble -- the string machine's chorus: a slow sweep and a fast one.
#
# fx/chorus.dsp is one LFO per side, and one LFO is a chorus: copies that
# drift sharp and flat together. A string ensemble (the Solina, the
# Juno's chorus II, a Dimension) modulates its bucket-brigade lines with
# two at once -- a slow sweep of about half a hertz and a shallow vibrato
# near six -- and that sum is what turns a sawtooth organ into a section.
#
# TWO CHORUSES IN SERIES PER SIDE, rather than one LFO summed from two,
# because delay::chorus carries its own LFO and the delay times of two
# lines in series add: the slow node's three taps feed the fast node's
# three, and every path through both is swept by both.
#
# THE WET IS DARKENED, because a bucket-brigade line is filtered either
# side of its clock to keep the clock out of the audio; `Tone' is that
# low-pass, and what makes this sound like hardware rather than a
# plug-in's clean copy.
#
# THE WET IS DOUBLED. Each chorus averages its taps, and nine paths that
# disagree in pitch average to about half the level of one; without the
# 6 dB back, turning `Mix' up would turn the channel down.
#
# Left and right start half a cycle apart, as in fx/chorus.dsp.

name "Ensemble";
author "Misha Nasledov";
description "A stereo string-ensemble chorus: a slow and a fast LFO on bucket-brigade-dark delay lines.";
category "Effects";

    @rate = 0.6;
    @rate.widget = 1;
    @rate.min = 0.1;
    @rate.max = 3;
    @rate.label = "Sweep Rate (Hz)";

    @depth = 2.5 ms;
    @depth.widget = 1;
    @depth.min = 0.1ms;
    @depth.max = 8ms;
    @depth.label = "Sweep Depth";

    @fast = 5.8;
    @fast.widget = 1;
    @fast.min = 2;
    @fast.max = 10;
    @fast.label = "Vibrato Rate (Hz)";

    @shimmer = 0.25 ms;
    @shimmer.widget = 1;
    @shimmer.min = 0ms;
    @shimmer.max = 1ms;
    @shimmer.label = "Vibrato Depth";

    @tone = 7000;
    @tone.widget = 1;
    @tone.min = 1500;
    @tone.max = 16000;
    @tone.label = "Tone (Hz)";

    @mix = 0.5;
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

node slowl delay::chorus {
    in = ionode->in0;
    rate = @rate;
    depth = @depth;
    delay = 8 ms;
    taps = 3;
    mix = 1;
    phase = 0;
};

node slowr delay::chorus {
    in = ionode->in1;
    rate = @rate;
    depth = @depth;
    delay = 8 ms;
    taps = 3;
    mix = 1;
    phase = 0.5;
};

node fastl delay::chorus {
    in = slowl->out;
    rate = @fast;
    depth = @shimmer;
    delay = 2 ms;
    taps = 3;
    mix = 1;
    phase = 0;
};

node fastr delay::chorus {
    in = slowr->out;
    rate = @fast;
    depth = @shimmer;
    delay = 2 ms;
    taps = 3;
    mix = 1;
    phase = 0.5;
};

node darkl filt::svf {
    in = fastl->out;
    cutoff = @tone;
    res = 0;
};

node darkr filt::svf {
    in = fastr->out;
    cutoff = @tone;
    res = 0;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = darkl->out_low * 2;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = darkr->out_low * 2;
    fade = @mix;
};

io ionode;
