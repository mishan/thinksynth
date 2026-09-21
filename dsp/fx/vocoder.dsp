# Vocoder -- one channel's spectrum wearing another channel's voice.
#
# The effect that could not be written here until an effect could hear a
# second channel. A vocoder needs two signals: the MODULATOR, whose
# shape is being copied -- a spoken line, a drum loop, anything with
# consonants -- and the CARRIER, which supplies the actual sound and has
# to be something with a continuous spectrum: a supersaw, `strings', a
# held chord. Neither is any use without the other, and until `side'
# there was nowhere to put the second one. Three vocoder graphs written
# before it read `input::wav' and a fourth `input::alsa', because there
# was no channel to name; all four are gone and this is what replaced
# them.
#
# This effect goes on the MODULATOR's channel, and the piece names the
# carrier:
#
#     instrument voice {
#         dsp    "ts1.dsp";
#         effect "fx/vocoder.dsp" { side = pads; };
#     };
#
# so `in0' is the modulator, which is this channel's own voices, and
# `side0' is the carrier. docs/GEN_FORMAT.md says what the clause is.
#
# SIXTEEN BANDS, a third of an octave apart, from 180 Hz to 5 kHz. Each
# band is a `filt::svf' band-pass on the modulator into an
# `env::follower', which is that band's loudness; the same band of the
# carrier is multiplied by it; the sixteen products are summed. That is
# the whole machine, and it is the machine a real one is: a bank of
# filters, a rectifier per band, and a second bank driven by the first.
#
# The spacing is a third of an octave because the ear's is. Vowels are
# told apart by where the first two formants sit -- around 300 to 900 Hz
# and 900 to 2500 -- and a band has to be narrow enough that a formant
# lands in one rather than smeared across three, and wide enough that
# sixteen of them cover the range a voice uses. 180 Hz at the bottom is
# under the lowest formant and above most of the pitch, which is what a
# vocoder is supposed to discard: the carrier decides the pitch, and a
# band that could hear the modulator's fundamental would let it back in.
#
# `Response' is the followers' falloff, in the units env::follower
# declares -- an exponent, each whole number ten times slower. This is
# the knob that decides whether consonants survive: too slow and the
# bands cannot rise and fall inside a syllable, too fast and each band
# tracks individual cycles of its own carrier and the output rasps. 2.2
# is a few milliseconds and is where a voice stays a voice.
#
# THE CONSONANTS ARE NOT IN THE BANDS. `s', `t', `f' and `sh' are noise
# above where any formant is, and sixteen band-passes ending at 5 kHz
# cannot carry them -- a carrier with nothing up there has nothing to
# multiply. So the modulator's own high end is passed straight through,
# unvocoded, which is what every hardware vocoder does and the reason
# they sound like they have teeth. `Sibilance' is how much of it, and
# `Sibilance (Hz)' is where it starts.
#
# MONO. The bands are built from the sum of both sides on each input and
# the result goes out to both, which is thirty-two filters rather than
# sixty-four. It is not only the cost: what a vocoder produces is one
# signal by construction -- one envelope per band, one product -- and
# two of them running on two sides would be two different vocoders
# disagreeing about which syllable it is. The width in a vocoded pad
# comes from the chorus or the reverb after it, not from here.
#
# `Level' is out in front because the output is quiet by nature: a sum
# of sixteen band-passes, each scaled by a number under 1, starts around
# a tenth of the carrier. 4 is a reasonable place to be.

name "Vocoder";
author "Misha Nasledov";
description "Sixteen bands of a carrier on another channel, driven by this channel's own spectrum.";

    @res = 0.89;
    @res.widget = 1;
    @res.min = 0.7;
    @res.max = 0.97;
    @res.label = "Band Q";

    @speed = 2.2;
    @speed.widget = 1;
    @speed.min = 1.6;
    @speed.max = 3.2;
    @speed.label = "Response";

    @hiss = 5000;
    @hiss.widget = 1;
    @hiss.min = 2000;
    @hiss.max = 12000;
    @hiss.label = "Sibilance (Hz)";

    @sibilance = 0.5;
    @sibilance.widget = 1;
    @sibilance.min = 0;
    @sibilance.max = 2;
    @sibilance.label = "Sibilance";

    @level = 4;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 16;
    @level.label = "Level";

node ionode {
    channels = 2;

    # This channel's voices: the modulator, whose shape is copied.
    in0 = 0;
    in1 = 0;

    # And the channel the piece named: the carrier, which is the sound
    # that actually comes out. Zeros where no side was given, so a
    # vocoder with nobody to vocode is silence rather than a fault.
    side0 = 0;
    side1 = 0;

    out0 = out->out;
    out1 = out->out;
};

# Both inputs summed to one. See the head: a vocoder is mono by
# construction, and this is where that is decided rather than in
# thirty-two more filters.
node mod math::mul {
    in0 = ionode->in0 + ionode->in1;
    in1 = 0.5;
};

node car math::mul {
    in0 = ionode->side0 + ionode->side1;
    in1 = 0.5;
};

# The analysis bank, the followers, and the synthesis bank: one
# band of the modulator decides how much of the same band of the
# carrier gets through.
node m00 filt::svf     { in = mod->out; cutoff = 180; res = @res; };
node e00 env::follower { in = m00->out_band; falloff = @speed; };
node c00 filt::svf     { in = car->out; cutoff = 180; res = @res; };
node v00 mixer::mul    { in0 = c00->out_band; in1 = e00->out; };
node m01 filt::svf     { in = mod->out; cutoff = 225; res = @res; };
node e01 env::follower { in = m01->out_band; falloff = @speed; };
node c01 filt::svf     { in = car->out; cutoff = 225; res = @res; };
node v01 mixer::mul    { in0 = c01->out_band; in1 = e01->out; };
node m02 filt::svf     { in = mod->out; cutoff = 280; res = @res; };
node e02 env::follower { in = m02->out_band; falloff = @speed; };
node c02 filt::svf     { in = car->out; cutoff = 280; res = @res; };
node v02 mixer::mul    { in0 = c02->out_band; in1 = e02->out; };
node m03 filt::svf     { in = mod->out; cutoff = 350; res = @res; };
node e03 env::follower { in = m03->out_band; falloff = @speed; };
node c03 filt::svf     { in = car->out; cutoff = 350; res = @res; };
node v03 mixer::mul    { in0 = c03->out_band; in1 = e03->out; };
node m04 filt::svf     { in = mod->out; cutoff = 437; res = @res; };
node e04 env::follower { in = m04->out_band; falloff = @speed; };
node c04 filt::svf     { in = car->out; cutoff = 437; res = @res; };
node v04 mixer::mul    { in0 = c04->out_band; in1 = e04->out; };
node m05 filt::svf     { in = mod->out; cutoff = 545; res = @res; };
node e05 env::follower { in = m05->out_band; falloff = @speed; };
node c05 filt::svf     { in = car->out; cutoff = 545; res = @res; };
node v05 mixer::mul    { in0 = c05->out_band; in1 = e05->out; };
node m06 filt::svf     { in = mod->out; cutoff = 680; res = @res; };
node e06 env::follower { in = m06->out_band; falloff = @speed; };
node c06 filt::svf     { in = car->out; cutoff = 680; res = @res; };
node v06 mixer::mul    { in0 = c06->out_band; in1 = e06->out; };
node m07 filt::svf     { in = mod->out; cutoff = 849; res = @res; };
node e07 env::follower { in = m07->out_band; falloff = @speed; };
node c07 filt::svf     { in = car->out; cutoff = 849; res = @res; };
node v07 mixer::mul    { in0 = c07->out_band; in1 = e07->out; };
node m08 filt::svf     { in = mod->out; cutoff = 1060; res = @res; };
node e08 env::follower { in = m08->out_band; falloff = @speed; };
node c08 filt::svf     { in = car->out; cutoff = 1060; res = @res; };
node v08 mixer::mul    { in0 = c08->out_band; in1 = e08->out; };
node m09 filt::svf     { in = mod->out; cutoff = 1323; res = @res; };
node e09 env::follower { in = m09->out_band; falloff = @speed; };
node c09 filt::svf     { in = car->out; cutoff = 1323; res = @res; };
node v09 mixer::mul    { in0 = c09->out_band; in1 = e09->out; };
node m10 filt::svf     { in = mod->out; cutoff = 1651; res = @res; };
node e10 env::follower { in = m10->out_band; falloff = @speed; };
node c10 filt::svf     { in = car->out; cutoff = 1651; res = @res; };
node v10 mixer::mul    { in0 = c10->out_band; in1 = e10->out; };
node m11 filt::svf     { in = mod->out; cutoff = 2061; res = @res; };
node e11 env::follower { in = m11->out_band; falloff = @speed; };
node c11 filt::svf     { in = car->out; cutoff = 2061; res = @res; };
node v11 mixer::mul    { in0 = c11->out_band; in1 = e11->out; };
node m12 filt::svf     { in = mod->out; cutoff = 2572; res = @res; };
node e12 env::follower { in = m12->out_band; falloff = @speed; };
node c12 filt::svf     { in = car->out; cutoff = 2572; res = @res; };
node v12 mixer::mul    { in0 = c12->out_band; in1 = e12->out; };
node m13 filt::svf     { in = mod->out; cutoff = 3210; res = @res; };
node e13 env::follower { in = m13->out_band; falloff = @speed; };
node c13 filt::svf     { in = car->out; cutoff = 3210; res = @res; };
node v13 mixer::mul    { in0 = c13->out_band; in1 = e13->out; };
node m14 filt::svf     { in = mod->out; cutoff = 4006; res = @res; };
node e14 env::follower { in = m14->out_band; falloff = @speed; };
node c14 filt::svf     { in = car->out; cutoff = 4006; res = @res; };
node v14 mixer::mul    { in0 = c14->out_band; in1 = e14->out; };
node m15 filt::svf     { in = mod->out; cutoff = 5000; res = @res; };
node e15 env::follower { in = m15->out_band; falloff = @speed; };
node c15 filt::svf     { in = car->out; cutoff = 5000; res = @res; };
node v15 mixer::mul    { in0 = c15->out_band; in1 = e15->out; };

node voiced math::mul {
    in0 = v00->out +
          v01->out +
          v02->out +
          v03->out +
          v04->out +
          v05->out +
          v06->out +
          v07->out +
          v08->out +
          v09->out +
          v10->out +
          v11->out +
          v12->out +
          v13->out +
          v14->out +
          v15->out;
    in1 = @level;
};

# The consonants, straight off the modulator and never through a band:
# what is above `Sibilance (Hz)' is noise rather than pitch, and noise
# is the one thing sixteen band-passes cannot reconstruct. See the head.
node hiss filt::svf {
    in = mod->out;
    cutoff = @hiss;
    res = 0;
};

node out math::add {
    in0 = voiced->out;
    in1 = hiss->out_high * @sibilance;
};

io ionode;
