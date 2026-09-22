# Vocoder (live in) -- the piece wearing the voice of whoever is in the room.
#
# fx/vocoder.dsp with the two signals coming from somewhere else. That one
# takes the MODULATOR from its own channel's voices and the CARRIER from a
# channel the piece named with `side'; this one takes the modulator from
# `live0' -- what the host is capturing, a microphone or a line in -- and the
# carrier from `in0', which is whatever this effect was put on.
#
# So the shortest way to use it is as the piece's own effect, where `in0' is
# the whole mix:
#
#     effect "fx/vocoder-mic.dsp";
#
# and that is the entire change to a piece. No instrument, no `side', no
# channel to name, because there is only one thing the machine is hearing and
# the io node asks for it by declaring it. A pad-heavy piece is already the
# carrier a vocoder wants: something with a continuous spectrum and no gaps.
#
# It works on one channel too -- `instrument voice { dsp "strings.dsp";
# effect "fx/vocoder-mic.dsp"; };' vocodes that instrument and leaves the
# rest of the piece alone -- which is the version to reach for when the
# drums should stay dry.
#
# EVERYTHING ELSE IS fx/vocoder.dsp, and its head is where the reasoning
# lives: sixteen bands a third of an octave apart from 180 Hz to 5 kHz
# because the ear's spacing is and a formant has to land in one band rather
# than three; `Response' as the followers' falloff, because that is the knob
# that decides whether consonants survive; the modulator's own high end
# passed through unvocoded, because `s', `t', `f' and `sh' are noise above
# where any formant is and sixteen band-passes cannot carry them; mono,
# because what a vocoder produces is one signal by construction; and `Level'
# out in front because a sum of sixteen band-passes starts quiet.
#
# WHAT IS DIFFERENT, and it is only this. `live0' and `live1' are handed the
# same signal -- the capture is mono (thSynth::feedCapture says why) -- so
# summing them the way fx/vocoder.dsp sums its two inputs would be summing
# one signal with itself. `mod' therefore reads `live0' alone.
#
# SILENCE WHERE NOTHING IS CAPTURING, which is what this should sound like
# with no microphone: the bands have no envelopes, so nothing of the carrier
# gets through. That is also what makes a piece carrying this render the same
# under genwav as it did yesterday -- an offline path feeds no capture and
# reads zeros.
#
# FEEDBACK IS REAL. A microphone, speakers and a graph that puts the one into
# the other is an oscillator, and the master limiter saturates it rather than
# preventing it. Headphones. On a page, the browser's echo canceller would
# also stop it, by removing whatever correlates with the output -- which here
# is the carrier, so it stops the vocoder too.

name "Vocoder (live in)";
author "Misha Nasledov";
description "Sixteen bands of this channel driven by what the machine is hearing.";

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

    # How much of the carrier goes out without passing through the bands.
    #
    # Zero by default, because a vocoder is a vocoder: everything you hear
    # should be the carrier wearing the modulator, and anything else is the
    # carrier leaking round the side of the machine.
    #
    # It is here anyway because of what the modulator is now. `fx/vocoder.dsp'
    # takes its modulator from a channel the piece is playing, so there is
    # always one; this one takes it from a microphone that may not be plugged
    # in, be switched on, or exist. Fully wet, that is a piece that is silent
    # until somebody talks -- fine for a vocoder and no way to ship a demo. A
    # piece that wants to be audible before anybody finds the microphone sets
    # this, and `gen/voice.gen' does.
    @dry = 0;
    @dry.widget = 1;
    @dry.min = 0;
    @dry.max = 1;
    @dry.label = "Dry";

node ionode {
    channels = 2;

    # The carrier: whatever this effect was put on. On a piece's own effect
    # clause that is the whole mix.
    in0 = 0;
    in1 = 0;

    # And the modulator: what the machine is hearing. Zeros where no host is
    # feeding one, so a vocoder with nobody talking into it is silence rather
    # than a fault.
    live0 = 0;

    out0 = out->out;
    out1 = out->out;
};

# Both sides of the carrier summed to one. See fx/vocoder.dsp's head: a
# vocoder is mono by construction, and this is where that is decided rather
# than in thirty-two more filters.
node car math::mul {
    in0 = ionode->in0 + ionode->in1;
    in1 = 0.5;
};

# The modulator is already mono, so there is nothing to sum and nothing to
# halve. Straight off the io node.
node mod math::mul {
    in0 = ionode->live0;
    in1 = 1;
};

# The analysis bank, the followers, and the synthesis bank: one band of the
# modulator decides how much of the same band of the carrier gets through.
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

# The consonants, straight off the modulator and never through a band: what is
# above `Sibilance (Hz)' is noise rather than pitch, and noise is the one thing
# sixteen band-passes cannot reconstruct.
node hiss filt::svf {
    in = mod->out;
    cutoff = @hiss;
    res = 0;
};

# The carrier, round the side. Doubled because `car' is the two inputs
# summed and halved, so `dry = 1' is the carrier at the level it came in at
# rather than half of it.
node dry math::mul {
    in0 = car->out;
    in1 = @dry * 2;
};

node out math::add {
    in0 = voiced->out + dry->out;
    in1 = hiss->out_high * @sibilance;
};

io ionode;
