# Limiter -- the last thing in the piece.
#
# A master effect: it runs on the sum of every channel, after the mix and
# before the engine's own soft clip, which is the only place a limiter
# can be. A limiter on a channel is not limiting the thing that clips --
# four channels each under a ceiling still add up to whatever they add up
# to -- and the mix is the one signal that is nobody's note.
#
# Two knobs and a follower between them. `Drive' is how hard the mix is
# pushed in; `Ceiling' is where it is not allowed past. The gain is
#
#     ceiling / max(level, ceiling)
#
# which is 1 while the level is under the ceiling and exactly the amount
# over when it is not, so nothing is touched until something has to be.
# The level is a peak follower, not the sample: a gain worked out per
# sample from the sample is a waveshaper, and what it shapes is the
# waveform rather than the loudness.
#
# `Smoothing' is that follower's falloff, in decades, and it is spelled
# the way env::follower spells it: a bigger number is a smaller
# coefficient and so a slower follower. At 1.5 the attack and the release
# are both under a millisecond, which catches most of a pluck's front; at
# 3 it is twenty-odd milliseconds and only the sustained loudness is
# caught. Faster ducks audibly, slower lets transients through to the
# engine's soft clip behind this -- which is there to catch exactly
# that, and is why this does not have to be a brickwall.
#
# ONE GAIN FOR BOTH SIDES, from the louder of the two. A limiter with a
# gain per side is a limiter that moves the stereo image every time one
# side is louder than the other, which on a piece with a kick panned
# anywhere but the middle is audible as the whole mix leaning.

name "Limiter";
author "Misha Nasledov";
description "A master limiter: a peak follower into a gain, on the sum of every channel.";
category "Effects";

    @drive = 1;
    @drive.widget = 1;
    @drive.min = 0.25;
    @drive.max = 8;
    @drive.label = "Drive";

    @ceiling = 0.9;
    @ceiling.widget = 1;
    @ceiling.min = 0.05;
    @ceiling.max = 1;
    @ceiling.label = "Ceiling";

    @smooth = 1.5;
    @smooth.widget = 1;
    @smooth.min = 1;
    @smooth.max = 3.5;
    @smooth.label = "Smoothing";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = outl->out;
    out1 = outr->out;
};

node folll env::follower {
    in = ionode->in0 * @drive;
    falloff = @smooth;
};

node follr env::follower {
    in = ionode->in1 * @drive;
    falloff = @smooth;
};

# The louder side, and then the ceiling under it: max(level, ceiling) is
# never less than the ceiling, so the ratio below is never more than 1
# and the limiter can only ever turn things down.
node loud math::max {
    in0 = folll->out;
    in1 = follr->out;
};

node over math::max {
    in0 = loud->out;
    in1 = @ceiling;
};

node outl mixer::mul {
    in0 = ionode->in0 * @drive;
    in1 = @ceiling / over->out;
};

node outr mixer::mul {
    in0 = ionode->in1 * @drive;
    in1 = @ceiling / over->out;
};

io ionode;
