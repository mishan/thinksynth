# Ladder -- two saws and a sub through a ladder filter with an envelope
# of its own.
#
# The shape of a monophonic lead from the early seventies: a pair of
# sawtooth oscillators a few hertz apart so they beat against each
# other, a square an octave down under them, a four-pole ladder lowpass
# whose cutoff is not the note's but the envelope's -- it opens on every
# attack and settles to where `cutoff' says -- and a little saturation
# after the filter, because the filter that clips a little is the sound.
#
# The filter's cutoff is normalised, 0 to 1, as filt::moog wants it:
# `cutoff' is where it rests, `fmax' is where the filter envelope throws
# it on each note, and the four `f' times are that envelope. The amp
# envelope is the usual one, with sustain scaled by velocity.
#
# `Cutoff Glide' is a misc::slew on the resting cutoff, and it is there
# for what a composer does to that knob rather than for what a player
# does: a gen::walk writing a new cutoff once a period is a staircase,
# and this is the lag that turns it into a line. A knob nothing moves is
# a knob nothing lags, so at a fixed cutoff the glide is inaudible by
# construction -- see the head of plugins/misc/slew.cpp for why a lag
# starts where its input is.

name "Ladder";
author "Misha Nasledov";
description "Two detuned saws and a sub octave through a ladder filter with its own envelope.";

    @detune = 1.3;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 12;
    @detune.label = "Detune (Hz)";

    @sub = 0.3;
    @sub.widget = 1;
    @sub.min = 0;
    @sub.max = 1;
    @sub.label = "Sub Octave";

    @cutoff = 0.18;
    @cutoff.widget = 1;
    @cutoff.min = 0.02;
    @cutoff.max = 1;
    @cutoff.label = "Cutoff";

    @glide = 40 ms;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 2000ms;
    @glide.label = "Cutoff Glide";

    @fmax = 0.6;
    @fmax.widget = 1;
    @fmax.min = 0.02;
    @fmax.max = 1;
    @fmax.label = "Envelope Peak";

    @res = 0.45;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";

    @drive = 1.6;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

    @fa = 4 ms;
    @fa.widget = 1;
    @fa.min = 0;
    @fa.max = 2000ms;
    @fa.label = "Filter Attack";
    @fd = 260 ms;
    @fd.widget = 1;
    @fd.min = 0;
    @fd.max = 5000ms;
    @fd.label = "Filter Decay";
    @fs = 0.25;
    @fs.widget = 1;
    @fs.min = 0;
    @fs.max = 1;
    @fs.label = "Filter Sustain";
    @fr = 200 ms;
    @fr.widget = 1;
    @fr.min = 0;
    @fr.max = 5000ms;
    @fr.label = "Filter Release";

    @a = 6 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000ms;
    @a.label = "Attack";
    @d = 120 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Decay";
    @s = 0.7;    # 1 = full, 0 = off
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @r = 120 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc1 osc::simple {
    freq = freq->out;
    waveform = 1;
};

# The second saw a few hertz off the first, so the beat is the same at
# every pitch; the sub an octave down. Both were a math:: node apiece and
# are arithmetic on the arg instead -- the same two nodes, built at load,
# named after the args they feed.
node osc2 osc::simple {
    freq = freq->out + @detune;
    waveform = 1;
};

node osc3 osc::simple {
    freq = freq->out * 0.5;
    waveform = 2;
};

# The filter's own envelope, from the floor to the peak and back.
node fenv env::adsr {
    a = @fa;
    d = @fd;
    s = @fs;
    r = @fr;
    trigger = ionode->trigger;
};

# The resting cutoff, lagged, so a knob that is stepped arrives as a ramp.
node cutglide misc::slew {
    in = @cutoff;
    time = @glide;
};

# How far up the envelope throws the cutoff is scaled by velocity: the
# peak is the whole distance from the resting cutoff only on a note
# played hard, so a soft note opens less far as well as sounding
# quieter.
node fmap env::map {
    in = fenv->out;
    inmin = 0;
    inmax = th_max;
    outmin = cutglide->out;
    outmax = cutglide->out + (@fmax - cutglide->out) * ionode->velocity;
};

# The two saws averaged, then faded against the sub. This was a pair of
# mixer::fade nodes -- `(a + b) * 0.5' and `mix*(1 - sub) + sub*osc3' are
# what each of them computed -- and is the same two multiplies and two
# adds either way, built at load and named after the arg they feed.
node filt filt::moog {
    in = (osc1->out + osc2->out) * 0.5 * (1 - @sub) + osc3->out * @sub;
    cutoff = fmap->out;
    res = @res;
};

node drive dist::saturate {
    in = filt->out_low;
    factor = @drive;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = ionode->velocity * @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = drive->out;
    in1 = env->out;
};

io ionode;
