# Cassette -- the whole mix played off a tape that has been played a lot.
#
# fx/tape.dsp is a tape echo: the wobble and the saturation are on the
# repeats. This is the machine the record was bounced to and played back
# on, so they are on everything.
#
#   WOW is the capstan's speed wandering, slowly and not in a cycle: a
#   misc::drift moving the read point of a one-tap delay::chorus, so the
#   pitch slides a few cents flat and sharp. Both sides read the same
#   drift, because a machine has one motor.
#
#   FLUTTER is the fast part, a few hertz, from the same chorus's own LFO.
#
#   THE TAPE ITSELF saturates, lifts the low end with the head bump
#   around 90 Hz, loses the top above `Tone', and hisses. The saturation
#   and the bump are divided back down by `Drive', so a quiet passage
#   keeps its level and only the peaks are rounded. The hiss is the tape
#   moving, so it comes in as the music does and goes a few seconds
#   after it stops: a piece not yet played is silent, and a pause inside
#   one keeps its hiss.
#
#   DROPOUTS are where the oxide is worn: now and then the level and the
#   top fall for a moment. A slower drift is the wear under the head;
#   where it rises past a threshold `Dropouts' sets, the tape dips.
#
# `Age' turns wow, flutter, hiss and dropouts up and down together; each
# of their own knobs is how much of it a fully worn tape has.
#
# THERE IS NO MIX. The wow delays the tape by a moving amount, so the dry
# signal beside it would comb-filter, with the notch sweeping.

name "Cassette";
author "Misha Nasledov";
description "A worn tape on the whole channel: wow, flutter, saturation, head bump, hiss and dropouts.";
category "Effects";

    @age = 0.5;
    @age.widget = 1;
    @age.min = 0;
    @age.max = 1;
    @age.label = "Age";

    @wow = 160;
    @wow.widget = 1;
    @wow.min = 0;
    @wow.max = 600;
    @wow.label = "Wow (samples)";

    @flutter = 3;
    @flutter.widget = 1;
    @flutter.min = 0;
    @flutter.max = 20;
    @flutter.label = "Flutter (samples)";

    @flutterrate = 6.5;
    @flutterrate.widget = 1;
    @flutterrate.min = 2;
    @flutterrate.max = 12;
    @flutterrate.label = "Flutter Rate (Hz)";

    @drive = 1.6;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

    @bump = 0.35;
    @bump.widget = 1;
    @bump.min = 0;
    @bump.max = 1;
    @bump.label = "Head Bump";

    @tone = 9000;
    @tone.widget = 1;
    @tone.min = 1500;
    @tone.max = 18000;
    @tone.label = "Tone (Hz)";

    @hiss = 0.03;
    @hiss.widget = 1;
    @hiss.min = 0;
    @hiss.max = 0.2;
    @hiss.label = "Hiss";

    # How long the hiss lasts after the last sound. Not a knob.
    @letgo = 3 s;

    @dropouts = 0.5;
    @dropouts.widget = 1;
    @dropouts.min = 0;
    @dropouts.max = 1;
    @dropouts.label = "Dropouts";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = tonel->out_low * (1 - 0.7 * dip->out);
    out1 = toner->out_low * (1 - 0.7 * dip->out);
};

# The read point, in samples behind the write: far enough back that the
# wow's swing and the flutter's together never reach the write.
node wow misc::drift {
    rate = 0.7;
    depth = @age * @wow;
    center = @age * (@wow + @flutter) + 2;
    seed = 1;
};

node wear misc::drift {
    rate = 3;
    depth = 1;
    center = 0;
    seed = 2;
};

# 0 on good tape, rising to 1 within a tenth of the drift's range past
# the threshold.
node dip math::clamp {
    in = ((wear->out - 1) + 0.6 * @age * @dropouts) * 5;
    lo = 0;
    hi = 1;
};

node motorl delay::chorus {
    in = ionode->in0;
    rate = @flutterrate;
    depth = @age * @flutter;
    delay = wow->out;
    taps = 1;
    mix = 1;
    phase = 0;
};

node motorr delay::chorus {
    in = ionode->in1;
    rate = @flutterrate;
    depth = @age * @flutter;
    delay = wow->out;
    taps = 1;
    mix = 1;
    phase = 0;
};

node hotl dist::saturate {
    in = motorl->out;
    factor = @drive;
};

node hotr dist::saturate {
    in = motorr->out;
    factor = @drive;
};

node bumpl filt::svf {
    in = hotl->out;
    cutoff = 90;
    res = 0.5;
};

node bumpr filt::svf {
    in = hotr->out;
    cutoff = 90;
    res = 0.5;
};

# Whether anything has been on the tape lately: 1 at any sound above
# -60 dB, letting go over three seconds after the last, and a moment's
# slew so the hiss comes in rather than switching on. The hold reads
# itself a sample back, a loop of two nodes.
node held math::max {
    in0 = min((abs(ionode->in0) + abs(ionode->in1)) * 1000, 1);
    in1 = held->out * exp2(-1.4427 / @letgo);
};

node moving misc::slew {
    in = min(held->out * 2, 1);
    time = 50 ms;
};

node hissl osc::noise {
    color = 1;
};

node hissr osc::noise {
    color = 1;
};

node tapel math::add {
    in0 = hotl->out / @drive + bumpl->out_band * @bump / @drive;
    in1 = hissl->out * @age * @hiss * moving->out;
};

node taper math::add {
    in0 = hotr->out / @drive + bumpr->out_band * @bump / @drive;
    in1 = hissr->out * @age * @hiss * moving->out;
};

node tonel filt::svf {
    in = tapel->out;
    cutoff = @tone * (1 - 0.7 * dip->out);
    res = 0;
};

node toner filt::svf {
    in = taper->out;
    cutoff = @tone * (1 - 0.7 * dip->out);
    res = 0;
};

io ionode;
