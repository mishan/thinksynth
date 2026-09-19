# Snare 808 -- two sines and a hiss, and `Snappy' is the mix.
#
# The 808 snare is not a drum with wires under it, the way `snare.dsp'
# is built. It is two resonators tuned a sixth apart -- 180 Hz and
# 330 Hz on the original -- rung by the trigger, plus a burst of noise
# through a high-pass, and one knob on the front panel called SNAPPY
# that sets how much of the noise goes out with them. Everything about
# how the sound reads is in that knob: down, it is a dry knock that
# disappears in a mix; up, it is all hiss and could be a hand clap.
#
# TWO SINES, NOT ONE. A single tuned sine is a tom. The second one a
# sixth above it beats against the first, and the beating is what the
# ear takes for the rattle of a real snare even before the noise
# arrives -- which is why the interval matters and why `Ratio' is a
# knob rather than a number in the file. 1.83 is the original; whole
# numbers make it a bell, and anything under about 1.2 is a flam.
#
# The noise is high-passed rather than band-passed: the 808's snappy
# path is a hiss with nothing under it, and a band would put a pitch
# back into a sound whose whole job is not to have one. `snare.dsp' uses
# a band-pass for the opposite reason, because a real snare's rattle
# does sit in a band.
#
# Three envelopes, all env::ad, all scaled by velocity through `p', so a
# quiet hit is quiet rather than short. The note ends when the longest
# of them does.

name "Snare 808";
author "Misha Nasledov";
description "Two beating sines under a high-passed hiss, mixed by Snappy: the 808 snare.";

    @tune = 180;
    @tune.widget = 1;
    @tune.min = 80;
    @tune.max = 500;
    @tune.label = "Tune (Hz)";

    @ratio = 1.83;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 4;
    @ratio.label = "Second Tone";

    @bd = 100 ms;
    @bd.widget = 1;
    @bd.min = 10ms;
    @bd.max = 800ms;
    @bd.label = "Tone Decay";

    @snappy = 0.55;
    @snappy.widget = 1;
    @snappy.min = 0;
    @snappy.max = 1;
    @snappy.label = "Snappy";

    @tone = 1800;
    @tone.widget = 1;
    @tone.min = 400;
    @tone.max = 8000;
    @tone.label = "Hiss Corner (Hz)";

    @nd = 180 ms;
    @nd.widget = 1;
    @nd.min = 10ms;
    @nd.max = 1500ms;
    @nd.label = "Hiss Decay";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = nenv->play;
};

node drum1 osc::simple {
    freq = @tune;
    waveform = 0;
};

node drum2 osc::simple {
    freq = @tune * @ratio;
    waveform = 0;
};

node denv env::ad {
    a = 0;
    d = @bd;
    p = ionode->velocity;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node hiss filt::svf {
    in = noise->out;
    cutoff = @tone;
    res = 0.1;
};

# The hiss is the longer of the two, so it is what `play' watches: a
# note that ended with the tones would cut the snare's tail off.
node nenv env::ad {
    a = 0;
    d = @nd;
    p = ionode->velocity;
};

# `Snappy' scales the noise against a fixed pair of tones rather than
# crossfading the two, which is what the panel knob did: turning it
# down leaves the knock where it was instead of making it louder.
node mix math::add {
    in0 = (drum1->out * 0.6 + drum2->out * 0.4) * denv->out * 0.75;
    in1 = hiss->out_high * nenv->out * @snappy * 0.8;
};

io ionode;
