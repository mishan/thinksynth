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
#   is divided back down by `Drive', so a quiet passage keeps its level
#   and only the peaks are rounded.
#
#   DROPOUTS are where the oxide is worn: now and then the level and the
#   top fall for a moment. A slower drift is the wear under the head;
#   where it rises past a threshold `Dropouts' sets, the tape dips.
#
# `Age' turns wow, flutter, hiss and dropouts up and down together; each
# of their own knobs is how much of it a fully worn tape has.

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

    @dropouts = 0.5;
    @dropouts.widget = 1;
    @dropouts.min = 0;
    @dropouts.max = 1;
    @dropouts.label = "Dropouts";

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

# 0 on good tape, rising to 1 within a fifth of the drift's range past
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

node hissl osc::noise {
    color = 1;
};

node hissr osc::noise {
    color = 1;
};

node tapel math::add {
    in0 = hotl->out / @drive + bumpl->out_band * @bump;
    in1 = hissl->out * @age * @hiss;
};

node taper math::add {
    in0 = hotr->out / @drive + bumpr->out_band * @bump;
    in1 = hissr->out * @age * @hiss;
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

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = tonel->out_low * (1 - 0.7 * dip->out);
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = toner->out_low * (1 - 0.7 * dip->out);
    fade = @mix;
};

io ionode;
