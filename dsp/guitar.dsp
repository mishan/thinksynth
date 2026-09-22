# Guitar -- the same string as ebass.dsp, played with a pick and a palm.
#
# A short burst of noise into a delay line a period long with a lowpass
# in its feedback path: the burst is the pick, the loop is the string,
# and what the two knobs at the front do is what a right hand does.
#
# DECAY is a time, not a feedback coefficient. The loop goes round `freq'
# times a second, so a coefficient that rings for a bar on the low E is
# gone in a blink two octaves up; what goes into the line is the
# coefficient that reaches -60 dB in `Decay' seconds at whatever the note
# is, and a chord voiced across two octaves rings evenly.
#
# PALM MUTE is the fraction of that left with the heel of the hand on the
# bridge. At 0.05 the string is gone in a twentieth of a second -- the
# chuck of a rhythm player, which is the whole of disco guitar -- and at
# 1 a chord rings for its `Decay'. Nothing else about the sound changes
# with it, which is what makes a scratching line and a ringing chorus the
# same instrument rather than two.
#
# PICK POSITION is a band-pass on the burst. A string plucked at the
# middle takes the low harmonics and one plucked at the bridge takes the
# high ones, because what the string holds is the shape it was let go
# from; filtering the excitation is the cheapest way of saying that and
# it is the right one. Low is a thumb over the neck pickup, high is a
# pick at the bridge, and past there it is a banjo.
#
# Polyphonic, because a guitar plays chords: six voices is six strings.
# A strum is not a knob here either -- `xform::harmonize' already
# staggers a chord's voices in time with `spread', so the piece writes
# the hand and the graph plays the string.
#
# Velocity is in the burst's level and in the damping, so a hard pick
# starts brighter and settles to where a soft one started -- a string
# does that, and an envelope on a filter only pretends to.

name "Guitar";
author "Misha Nasledov";
description "A picked string with a palm mute: the rhythm guitar.";
category "Plucked";

    @pluck = 2 ms;
    @pluck.widget = 1;
    @pluck.min = 0.5ms;
    @pluck.max = 20ms;
    @pluck.label = "Pick Length";

    @pick = 900;
    @pick.widget = 1;
    @pick.min = 200;
    @pick.max = 4000;
    @pick.label = "Pick Position (Hz)";

    # Seconds, and deliberately a plain number: a unit in a .dsp is folded
    # into samples, and what the exponent below wants is seconds.
    @decay = 1.2;
    @decay.widget = 1;
    @decay.min = 0.05;
    @decay.max = 8;
    @decay.label = "String Decay (s)";

    @mute = 1;
    @mute.widget = 1;
    @mute.min = 0.05;
    @mute.max = 1;
    @mute.label = "Palm Mute";

    @tone = 0.62;
    @tone.widget = 1;
    @tone.min = 0;
    @tone.max = 0.9;
    @tone.label = "String Damping";

    @bright = 0.25;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 0.6;
    @bright.label = "Pick Brightness";

    @cutoff = 3600;
    @cutoff.widget = 1;
    @cutoff.min = 500;
    @cutoff.max = 12000;
    @cutoff.label = "Amp Tone (Hz)";

    @drive = 1.4;
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
    poly = 6;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The pick: a couple of milliseconds of it, as hard as the note was
# played.
node burst env::ad {
    a = 0;
    d = @pluck;
    p = ionode->velocity;
};

node pluck mixer::mul {
    in0 = noise->out;
    in1 = burst->out;
};

# Where the hand is. A band rather than a corner: a string let go at one
# point along its length has a notch in what it holds either side of it,
# and the band is the half of that the ear reads as position. It also
# keeps DC out of a loop that would pass it happily.
node pos filt::svf {
    in = pluck->out * 2;
    cutoff = @pick;
    res = 0.3;
};

# The string. `feedback' is what is left of a pass, which is
# 10^(-3/(decay * freq)) for a decay of 60 dB over `Decay' seconds. The
# line holds a tenth of a second, three octaves below the lowest note a
# guitar has.
node string filt::comb {
    in = pos->out_band;
    freq = freq->out;
    feedback = clamp(pow(10, -3 / (@decay * @mute * freq->out)), 0, 0.9995);
    damp = clamp(@tone - ionode->velocity * @bright, 0, 0.9);
    size = 100 ms;
};

node amp filt::svf {
    in = string->out * 1.4;
    cutoff = @cutoff;
    res = 0.15;
};

node drive dist::saturate {
    in = amp->out_low;
    factor = @drive;
};

# The string's own decay is the note's; this shapes the ends. The release
# is the hand landing on the strings, which is how a chuck stops.
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
