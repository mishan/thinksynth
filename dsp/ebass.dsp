# Electric Bass -- a string, plucked with a burst of noise.
#
# Nothing here oscillates. A few milliseconds of noise goes into a delay
# line a period long with a lowpass in its feedback path, and what comes
# out is a plucked string: the burst is the finger, the loop length is
# the pitch, `String Decay' is how long the string rings and `String
# Damping' is how fast it loses its top. That is the Karplus-Strong model, and it
# is a string rather than a filtered sawtooth because the harmonics do
# not all decay together -- which is the thing an oscillator through an
# envelope cannot do at any setting.
#
# PLUCK BRIGHTNESS is the other half of it. A string pulled harder is not
# only louder: the corner it is bent into holds more of the high
# harmonics, so a hard note starts brighter and then darkens to where a
# quiet one started. Velocity is therefore in two places -- the burst's
# level, and the damping it plays into -- and a line whose steps sit at
# 60 and 110 sounds like a player digging in rather than a fader moving.
#
# DECAY is a time and not a feedback coefficient, which matters because
# the loop goes round `freq' times a second: a coefficient that rings for
# a second on the low E is gone in a tenth two octaves up, and a bass
# part that sustains at the bottom and clicks at the top is the first
# thing anybody notices. What the graph writes into the line is therefore
# the coefficient that reaches -60 dB in `Decay' seconds at whatever the
# note is, which is one `pow' and an instrument that plays evenly.
#
# PALM MUTE is the fraction of that decay left with the heel of the hand
# on the bridge: at 1 the string rings for its `Decay', and at 0.05 a
# note is a thud a sixteenth long. That is the octave line the whole
# genre is built on, and it is a knob a piece can write per channel.
#
# The burst is high-passed before it reaches the line. A delay line with
# feedback passes DC as happily as it passes the fundamental, so a burst
# with a little offset in it leaves an offset in the loop that outlives
# the note; taking it off first is also what a finger does, being nearer
# the bridge than the middle of the string.
#
# `mono = 1', so overlapping notes slide: the glide is the hammer-on and
# the slide up the neck, and a gap between the notes is a fresh pluck.
# Against that the amp envelope holds at full and only shapes the ends,
# because the decay of this instrument is the string's and not an
# envelope's; the release is the hand coming down on the strings.

name "Electric Bass";
author "Misha Nasledov";
description "A noise burst into a damped delay line: the fingered electric bass.";

    @glide = 40 ms;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 500ms;
    @glide.label = "Glide";

    @pluck = 5 ms;
    @pluck.widget = 1;
    @pluck.min = 1ms;
    @pluck.max = 40ms;
    @pluck.label = "Pluck Length";

    @finger = 80;
    @finger.widget = 1;
    @finger.min = 30;
    @finger.max = 1200;
    @finger.label = "Pluck Corner (Hz)";

    # Seconds, and deliberately a plain number: a unit in a .dsp is folded
    # into samples, and what the exponent below wants is seconds.
    @decay = 2.5;
    @decay.widget = 1;
    @decay.min = 0.05;
    @decay.max = 8;
    @decay.label = "String Decay (s)";

    @mute = 1;
    @mute.widget = 1;
    @mute.min = 0.05;
    @mute.max = 1;
    @mute.label = "Palm Mute";

    @tone = 0.55;
    @tone.widget = 1;
    @tone.min = 0;
    @tone.max = 0.9;
    @tone.label = "String Damping";

    @bright = 0.3;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 0.6;
    @bright.label = "Pluck Brightness";

    @cutoff = 2200;
    @cutoff.widget = 1;
    @cutoff.min = 300;
    @cutoff.max = 8000;
    @cutoff.label = "Body Cutoff (Hz)";

    @drive = 1.8;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

    @a = 1 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 100ms;
    @a.label = "Attack";

    @r = 100 ms;
    @r.widget = 1;
    @r.min = 5ms;
    @r.max = 1000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    mono = 1;
    poly = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The slide. Its state belongs to the voice, so it carries across the
# retune a `mono' channel does and the pitch walks into the new note --
# and the line's own length is what it walks, which is a bend on a
# fretless rather than a filter sweep.
node glide misc::slew {
    in = freq->out;
    time = @glide;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The finger: a few milliseconds of it, as hard as the note was played.
node burst env::ad {
    a = 0;
    d = @pluck;
    p = ionode->velocity;
};

node pluck mixer::mul {
    in0 = noise->out;
    in1 = burst->out;
};

node pick filt::svf {
    in = pluck->out;
    cutoff = @finger;
    res = 0.2;
};

# The string. `freq' is the loop's length, so it is the pitch; `damp' is
# the lowpass inside the loop, which is why the top goes first; and
# `feedback' is what is left of a pass, which is 10^(-3/(decay * freq))
# for a decay of 60 dB over `Decay' seconds. The line holds a fifth of a
# second, which is two and a half octaves below the lowest note a bass
# has.
node string filt::comb {
    in = pick->out_high;
    freq = glide->out;
    feedback = clamp(pow(10, -3 / (@decay * @mute * glide->out)), 0, 0.9995);
    damp = clamp(@tone - ionode->velocity * @bright, 0, 0.9);
    size = 200 ms;
};

# The body and the amp: a pickup hears a string through the wood and the
# wire, and neither of them carries the top of what the string is doing.
node body filt::svf {
    in = string->out * 2;
    cutoff = @cutoff;
    res = 0.1;
};

node drive dist::saturate {
    in = body->out_low;
    factor = @drive;
};

# Held at full: the string's own decay is the note's, and this only
# shapes the ends. The release is the hand coming down.
node env env::adsr {
    a = @a;
    d = 1 ms;
    s = 1;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = drive->out;
    in1 = env->out;
};

io ionode;
