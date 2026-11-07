# Compressor -- the pump that is the kick, rather than a drawing of it.
#
# `dyn::compressor' on a channel's sum: a threshold, a ratio, an attack,
# a release, a knee and a makeup gain. On its own it is what a
# compressor is anywhere -- a snare with a slow attack keeps its crack
# and loses its length, a bass with a fast one stops moving around --
# and with `side' it is the other thing entirely.
#
# THE SIDECHAIN. A piece names the channel the key comes from:
#
#     instrument bass {
#         dsp    "bass.dsp";
#         effect "fx/comp.dsp" { side = kick; };
#     };
#
# and every kick turns the bass down and lets it back up over the
# release. That is the pump under every dance record since about 1982,
# and it is a different thing from a composer drawing one: `gen::pump'
# emits a dip in an amp once a beat because until an effect could hear a
# second channel there was nowhere else for a sidechain to live, and
# what it knows about is the beats it wrote rather than the kick. A
# kick that is late, quiet, missing, or two kicks in a bar all land
# here and none of them land there.
#
# THERE IS NO KNOB FOR "IS THERE A SIDE", because there does not need to
# be one. `side0' with nobody named is this channel's own audio, so the
# key below is the kick where a piece named the kick and the signal
# itself where it named nothing -- and this file is an ordinary
# compressor and a sidechain compressor without knowing which it is.
#
# ONE GAIN FOR BOTH SIDES. The compressor node reads the sum of the two
# and its `gain' output -- the reduction, in dB -- drives both through
# `misc::decibel'. A gain worked out per side is a compressor that moves
# the stereo image every time one side is louder than the other, which
# fx/limiter.dsp says at greater length and for the same reason.
#
# The node's own `out' is unused here, and that is deliberate: what is
# wanted from it is the gain, so that the makeup and the two sides are
# applied where they can be seen.

name "Compressor";
author "Misha Nasledov";
description "A compressor on a channel's sum, keyed from its own signal or from the channel named as its side.";
category "Effects";

    @threshold = -18;
    @threshold.widget = 1;
    @threshold.min = -48;
    @threshold.max = 0;
    @threshold.label = "Threshold (dB)";

    @ratio = 4;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 20;
    @ratio.label = "Ratio";

    @attack = 10 ms;
    @attack.widget = 1;
    @attack.min = 0.1 ms;
    @attack.max = 200 ms;
    @attack.label = "Attack";

    @release = 150 ms;
    @release.widget = 1;
    @release.min = 5 ms;
    @release.max = 1000 ms;
    @release.label = "Release";

    @knee = 6;
    @knee.widget = 1;
    @knee.min = 0;
    @knee.max = 24;
    @knee.label = "Knee (dB)";

    @makeup = 0;
    @makeup.widget = 1;
    @makeup.min = 0;
    @makeup.max = 24;
    @makeup.label = "Makeup (dB)";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    # And these: the channel the piece named as the side, or this one
    # again where it named none.
    side0 = 0;
    side1 = 0;

    out0 = outl->out;
    out1 = outr->out;
};

# What the level is measured from: the side's two channels summed. With
# no side named that is this channel, which is the ordinary compressor.
node key math::mul {
    in0 = ionode->side0 + ionode->side1;
    in1 = 0.5;
};

# The detector. `in' is the key as well, so that the node's own `gain'
# is the reduction this key asks for; what that reduction is applied to
# is below, one multiply per side.
node det dyn::compressor {
    in = key->out;
    side = key->out;
    threshold = @threshold;
    ratio = @ratio;
    attack = @attack;
    release = @release;
    knee = @knee;
    makeup = 0;
};

# dB into a gain, makeup and all: `det->gain' is negative or zero, so
# with no makeup this is never above 1.
node amount misc::decibel {
    db = det->gain + @makeup;
};

node outl mixer::mul {
    in0 = ionode->in0;
    in1 = amount->out;
};

node outr mixer::mul {
    in0 = ionode->in1;
    in1 = amount->out;
};

io ionode;
