# Glue -- the mix bus: EQ, a slow compressor, tape, width and a ceiling.
#
# What a mastering chain does to a finished mix, as one master effect.
# None of it is dramatic and that is the point: each stage is a few dB,
# and together they are the difference between a set of channels and a
# record.
#
# THE LOW CUT comes first, because everything after it reacts to level:
# rumble the ear cannot hear still pumps a compressor. It sits at 20 Hz
# and not higher because a pad's sub-octave and a low kick live just
# above that, and this filter's slope is gentle.
#
# TWO SHELVES, `Low' and `High' in dB. Each is a state-variable filter
# whose low or high output is added back to the signal scaled by the
# gain less one, so 0 dB is the signal untouched.
#
# THE COMPRESSOR IS SLOW AND GENTLE: 2 to 1, a soft knee, 30 ms to let
# the drums' attack through and a quarter second to come back. Fast
# enough to hold the mix together, slow enough that you hear a band
# breathing and not a meter. It is keyed from the sum and applied to
# both sides, so the image does not lean when one side is louder.
#
# TAPE is a tanh scaled back down by its own drive, which is unity for
# a quiet signal and rounds the peaks of a loud one: `Tape' at 1 is
# barely there, at 3 it is a cassette deck in the red.
#
# WIDTH is mid and side: the side signal times `Width', so 1 is as
# mixed, 0 is mono and 1.3 opens the stereo of the pads and the
# reverbs without moving the kick and bass, which sit in the middle.
#
# THE CEILING is the limiter from `fx/limiter.dsp', last, so a piece
# that wears this one in place of that one keeps its peaks under
# `Ceiling' all the same.

name "Glue";
author "Misha Nasledov";
description "A mix-bus chain for a piece: low cut, shelving EQ, a gentle compressor, tape saturation, width and a ceiling.";
category "Effects";

    @lowcut = 20;
    @lowcut.widget = 1;
    @lowcut.min = 10;
    @lowcut.max = 120;
    @lowcut.label = "Low Cut (Hz)";
    @lowcut.group = "EQ";

    @low = 1.5;
    @low.widget = 1;
    @low.min = -12;
    @low.max = 12;
    @low.label = "Low (dB)";
    @low.group = "EQ";

    @lowf = 110;
    @lowf.widget = 1;
    @lowf.min = 40;
    @lowf.max = 400;
    @lowf.label = "Low Freq (Hz)";
    @lowf.group = "EQ";

    @high = 2;
    @high.widget = 1;
    @high.min = -12;
    @high.max = 12;
    @high.label = "High (dB)";
    @high.group = "EQ";

    @highf = 8000;
    @highf.widget = 1;
    @highf.min = 2000;
    @highf.max = 16000;
    @highf.label = "High Freq (Hz)";
    @highf.group = "EQ";

    @threshold = -16;
    @threshold.widget = 1;
    @threshold.min = -40;
    @threshold.max = 0;
    @threshold.label = "Threshold (dB)";
    @threshold.group = "Compressor";

    @ratio = 2;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 10;
    @ratio.label = "Ratio";
    @ratio.group = "Compressor";

    @attack = 30 ms;
    @attack.widget = 1;
    @attack.min = 1 ms;
    @attack.max = 100 ms;
    @attack.label = "Attack";
    @attack.group = "Compressor";

    @release = 250 ms;
    @release.widget = 1;
    @release.min = 50 ms;
    @release.max = 1000 ms;
    @release.label = "Release";
    @release.group = "Compressor";

    @makeup = 2;
    @makeup.widget = 1;
    @makeup.min = 0;
    @makeup.max = 12;
    @makeup.label = "Makeup (dB)";
    @makeup.group = "Compressor";

    @tape = 1.2;
    @tape.widget = 1;
    @tape.min = 0.5;
    @tape.max = 4;
    @tape.label = "Tape";

    @width = 1.2;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 2;
    @width.label = "Width";

    @ceiling = 0.9;
    @ceiling.widget = 1;
    @ceiling.min = 0.1;
    @ceiling.max = 1;
    @ceiling.label = "Ceiling";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = outl->out;
    out1 = outr->out;
};

node cutl filt::svf { in = ionode->in0; cutoff = @lowcut; res = 0; };
node cutr filt::svf { in = ionode->in1; cutoff = @lowcut; res = 0; };

node lowgain  misc::decibel { db = @low; };
node highgain misc::decibel { db = @high; };

node lshl filt::svf { in = cutl->out_high; cutoff = @lowf; res = 0; };
node lshr filt::svf { in = cutr->out_high; cutoff = @lowf; res = 0; };
node hshl filt::svf { in = cutl->out_high; cutoff = @highf; res = 0; };
node hshr filt::svf { in = cutr->out_high; cutoff = @highf; res = 0; };

node eql math::add {
    in0 = cutl->out_high + lshl->out_low * (lowgain->out - 1);
    in1 = hshl->out_high * (highgain->out - 1);
};
node eqr math::add {
    in0 = cutr->out_high + lshr->out_low * (lowgain->out - 1);
    in1 = hshr->out_high * (highgain->out - 1);
};

node det dyn::compressor {
    in = (eql->out + eqr->out) * 0.5;
    threshold = @threshold;
    ratio = @ratio;
    attack = @attack;
    release = @release;
    knee = 6;
    makeup = 0;
};

node squeeze misc::decibel {
    db = det->gain + @makeup;
};

node tapel dist::saturate { in = eql->out * squeeze->out; factor = @tape; };
node taper dist::saturate { in = eqr->out * squeeze->out; factor = @tape; };

# Mid and side, the side scaled, back to left and right.
node widel math::add {
    in0 = (tapel->out + taper->out) * 0.5 / @tape;
    in1 = (tapel->out - taper->out) * 0.5 * @width / @tape;
};
node wider math::add {
    in0 = (tapel->out + taper->out) * 0.5 / @tape;
    in1 = (taper->out - tapel->out) * 0.5 * @width / @tape;
};

node folll env::follower { in = widel->out; falloff = 1.5; };
node follr env::follower { in = wider->out; falloff = 1.5; };

node over math::max {
    in0 = max(folll->out, follr->out);
    in1 = @ceiling;
};

node outl mixer::mul { in0 = widel->out; in1 = @ceiling / over->out; };
node outr mixer::mul { in0 = wider->out; in1 = @ceiling / over->out; };

io ionode;
