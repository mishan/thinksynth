# Drive -- a saturation that gets dirtier the harder the channel is hit.
#
# The distortion the corpus did not have. `fx/tape' saturates, but it
# does it to a delay's repeats and as part of being a tape; there was
# nothing to put across a channel and turn up.
#
# `dist::inksat' rather than the `dist::saturate' every instrument
# reaches for, because the two are different shapes and only one of them
# is interesting here. tanh rounds the peaks and leaves everything under
# them alone, so quiet playing stays clean -- which is what a drum bus
# wants and is why instruments use it. inksat raises the magnitude to a
# power: out = |in|^(1/(Drive + 1)), keeping the sign. Every sample moves
# toward full scale, and the quieter it was the further it travels. At
# `Drive = 4' a signal 40 dB down comes back 8 dB down. That is not a
# soft clipper, it is a shape that eats dynamics whole, and the sound is
# a fuzz pedal rather than an overdriven valve.
#
# WHICH IS WHY THERE IS A GATE, and why it is not optional decoration.
# A curve that lifts everything toward full scale lifts the noise floor
# and the tail of the last note with it: without one, the space between
# two chords is a roar. `Gate' is the level below which nothing gets
# through, and it is the first knob to set.
#
# `Dynamics' is the idea worth keeping out of the three graphs this
# replaces. The saturation amount is not a constant: it is
# `Drive + Dynamics * level', so how hard the channel is playing decides
# how dirty it gets -- the response a pedal has and a waveshaper on its
# own cannot.
#
# Measured on `bass.dsp' at four channel amplitudes, as the ratio of RMS
# to peak, which is how much of the waveform has been flattened. The dry
# signal sits at 0.41 at every level, because an instrument sounds the
# same shape however loud it is played:
#
#            channel amp      5     20     60    100
#     Dynamics = 0         0.404  0.466  0.466  0.466
#     Dynamics = 8         0.402  0.484  0.575  0.644
#
# At 0 that is one fixed amount of fuzz whatever is played through it.
# At 8 the quiet end is 0.402 against the dry 0.410 -- clean, to within
# the measurement -- and the loud end is squared off. That is the whole
# knob, and it is the reason this is an effect and not a curve.
#
# `Response' is the follower's falloff in the units `env::follower'
# declares, an exponent where each whole number is ten times slower.
# A peak follower rather than `env::followavg': a pedal reacts to the
# attack, and an average would have the grit arrive a moment after the
# note that earned it. Under about 1 a rise overshoots rather than
# tracking, which is where the knob stops.
#
# THE TONE CONTROL IS AFTER THE SATURATION, which is the order an amp
# puts it in and the only order that helps. Distortion makes harmonics;
# a filter in front of it only decides which ones, while a filter behind
# it can take the ones that hurt back off. `Bite' is resonance on the
# same filter, for the mid-range honk a small speaker has.
#
# STEREO, WITH ONE ENVELOPE. The shaper and the filter run on each side
# separately, because saturation is a per-sample curve and running it
# twice is what keeps the image where it was. The level that drives
# `Dynamics' and the gate is taken from the sum, for the reason
# `fx/gate' and `fx/wah' give: two envelopes disagreeing about how loud
# the channel is would open the gate on one speaker first.

name "Drive";
author "Misha Nasledov";
description "A gated power-curve saturation whose dirt follows how hard the channel is played.";

    @drive = 0.3;
    @drive.widget = 1;
    @drive.min = 0;
    @drive.max = 8;
    @drive.label = "Drive";

    @dynamics = 1.5;
    @dynamics.widget = 1;
    @dynamics.min = 0;
    @dynamics.max = 8;
    @dynamics.label = "Dynamics";

    @speed = 2.4;
    @speed.widget = 1;
    @speed.min = 1;
    @speed.max = 4;
    @speed.label = "Response";

    @gate = 0.02;
    @gate.widget = 1;
    @gate.min = 0;
    @gate.max = 0.2;
    @gate.label = "Gate";

    @tone = 3000;
    @tone.widget = 1;
    @tone.min = 300;
    @tone.max = 12000;
    @tone.label = "Tone (Hz)";

    @res = 0.2;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Bite";

    @level = 0.5;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# How hard the channel is being played, in one number, from both sides.
node level env::follower {
    in = (ionode->in0 + ionode->in1) * 0.5;
    falloff = @speed;
};

# The gate, as a gain rather than as a `misc::noisegate' on each side:
# one decision from the sum, applied to both. It reaches fully open a
# fortieth of full scale under `Gate', so there is a knee rather than a
# click, and at `Gate = 0' the expression is 1 plus a positive number
# and the clamp holds it open however quiet the channel goes.
node open math::clamp {
    in = 1 - (@gate - level->out) * 40;
    lo = 0;
    hi = 1;
};

# out = |in|^(1/(factor+1)), and `factor' is where `Dynamics' lands: the
# knob is a constant, the playing is not, and the sum of the two is what
# shapes this window.
node satl dist::inksat {
    in = ionode->in0 * open->out;
    factor = @drive + @dynamics * level->out;
};

node satr dist::inksat {
    in = ionode->in1 * open->out;
    factor = @drive + @dynamics * level->out;
};

node tonel filt::svf {
    in = satl->out;
    cutoff = @tone;
    res = @res;
};

node toner filt::svf {
    in = satr->out;
    cutoff = @tone;
    res = @res;
};

# `Level' defaults well under unity on purpose: a curve that pushes
# every sample toward full scale hands back something close to a square
# wave, and at `Drive' past about 4 the wet signal is louder than
# anything that went in.
node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = tonel->out_low * @level;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = toner->out_low * @level;
    fade = @mix;
};

io ionode;
