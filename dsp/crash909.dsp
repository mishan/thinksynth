# Crash -- the ride with no ping, more hiss and four times the tail.
#
# `ride909.dsp' with four numbers moved, and the file says so rather
# than pretending to be a second instrument. A crash and a ride are the
# same object hit in a different place: struck at the edge instead of
# near the bell, a cymbal puts its energy into hundreds of high modes at
# once rather than a few low ones, so there is no pitch to hold time
# with, the attack is a wall rather than a tick, and the plate takes
# several seconds to give it all back. That is more partials, tuned
# higher, spread wider, far more noise, no bell, and a long decay --
# which is exactly the list of what differs below.
#
# The band still falls as the note rings out, for the reason the ride's
# does: metal loses its high modes first. On a crash the fall is most of
# what the tail *is*, because there is nothing tonal underneath it to
# hear instead, so `Open' and `Close' are further apart here than they
# are there.
#
# A crash is one hit a phrase. `xform::chance' on a downbeat, or
# `gen::form' naming the bar, is how a piece asks for one; a crash on
# every bar is a record nobody finished listening to.

name "Crash 909";
author "Misha Nasledov";
description "Twelve inharmonic partials over noise with a long falling band: the crash cymbal.";
category "Drums";

    @freq = 900;
    @freq.widget = 1;
    @freq.min = 200;
    @freq.max = 3000;
    @freq.label = "Tune (Hz)";

    @ratio = 1.52;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 3;
    @ratio.label = "Ratio";

    @spread = 610;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 3000;
    @spread.label = "Spread (Hz)";

    @hiss = 0.55;
    @hiss.widget = 1;
    @hiss.min = 0;
    @hiss.max = 1;
    @hiss.label = "Hiss";

    @open = 11000;
    @open.widget = 1;
    @open.min = 2000;
    @open.max = 18000;
    @open.label = "Open (Hz)";

    @close = 1800;
    @close.widget = 1;
    @close.min = 300;
    @close.max = 12000;
    @close.label = "Close (Hz)";

    @res = 0.15;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Band Width";

    # A few milliseconds of it, so the attack is a wall and not an edge.
    # A crash is the one drum in a kit whose front is not a click.
    @swell = 6 ms;
    @swell.widget = 1;
    @swell.min = 0;
    @swell.max = 120ms;
    @swell.label = "Swell";

    @decay = 3400 ms;
    @decay.widget = 1;
    @decay.min = 200ms;
    @decay.max = 12000ms;
    @decay.label = "Decay";

node ionode {
    channels = 2;
    out0 = air->out_low * 1.6;
    out1 = air->out_low * 1.6;
    play = env->play;
};

node metal osc::multiwave {
    waves = 12;
    freq = @freq;
    amp = 0.9;
    pitchmul = @ratio;
    pitchadd = @spread;
    ampmul = 0.9;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node fenv env::ad {
    a = 0;
    d = @decay;
};

node band filt::svf {
    in = metal->out * 0.4 + noise->out * @hiss * 0.6;
    cutoff = @close + fenv->out * (@open - @close);
    res = @res;
};

node env env::ad {
    a = @swell;
    d = @decay;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = band->out_band * 0.8 + band->out_high * 0.4;
    in1 = env->out;
};

# The ear is most sensitive between two and five kilohertz, and partials
# low enough to land there make a cymbal clang and bite rather than
# shimmer. Two gentle high-pass stages at 4000 Hz, each letting half of
# the band below through, lower that body by four to six decibels against
# the sizzle above it, and a low-pass at 12.5 kHz takes the fizz off the
# top; the gain puts the sizzle back where it was.
node soft filt::svf {
    in = out->out;
    cutoff = 4000;
    res = 0;
};

node soft2 filt::svf {
    in = soft->out_high + soft->out_band * 0.5;
    cutoff = 4000;
    res = 0;
};

node air filt::svf {
    in = soft2->out_high + soft2->out_band * 0.5;
    cutoff = 12500;
    res = 0;
};

io ionode;
