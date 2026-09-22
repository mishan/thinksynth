# Kit Kick -- a drum that was hit, not a machine that was triggered.
#
# The dance-floor kicks in this tree are circuits: kick909.dsp drops a
# sine seventy milliseconds and clips it, kick808.dsp rings one for a
# second and a half. A bass drum in a room is three things arriving at
# once instead, and the difference between them is the whole sound:
#
#   THE HEAD, a sine whose pitch falls an octave in about thirty
#   milliseconds. Fast, because a struck head is stretched hardest at
#   the moment of contact and slackens immediately -- the 909's seventy
#   milliseconds is a machine imitating the gesture, and a real one is
#   over before the ear can follow it as a pitch at all.
#
#   THE SHELL, a band under the head that the head rings rather than
#   plays: the air in the drum, at `Shell'. It is fed by the beater and
#   not by the tone, because what excites a shell is the hit.
#
#   THE BEATER, a band-passed click a few milliseconds long. The felt on
#   a wooden beater is two or three kilohertz of noise and nothing else,
#   and it is what a kick sounds like through a small speaker.
#
# `Tune' is in hertz and the note number is ignored -- a kick is a kick,
# and a kit that transposed with the part would be four drums.
#
# THE CLICK IS AN ENVELOPE, not an `impulse::' node. Those allocate a
# buffer of their own length and a reader indexes it modulo that length
# *within a window*, so one used as a source repeats once a window and
# renders differently at 256 samples than at 1024. See rim808.dsp, which
# found it first. An env::ad click is one click, over when it says it is.

name "Kit Kick";
author "Misha Nasledov";
description "A head that falls an octave, a shell it rings and a beater click: the acoustic kick.";
category "Drums";

    @tune = 55;
    @tune.widget = 1;
    @tune.min = 30;
    @tune.max = 120;
    @tune.label = "Tune (Hz)";

    @sweep = 30 ms;
    @sweep.widget = 1;
    @sweep.min = 2ms;
    @sweep.max = 200ms;
    @sweep.label = "Head Sweep";

    @decay = 260 ms;
    @decay.widget = 1;
    @decay.min = 40ms;
    @decay.max = 2000ms;
    @decay.label = "Decay";

    @shell = 80;
    @shell.widget = 1;
    @shell.min = 40;
    @shell.max = 300;
    @shell.label = "Shell (Hz)";

    @body = 0.5;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1;
    @body.label = "Shell Level";

    @beater = 0.45;
    @beater.widget = 1;
    @beater.min = 0;
    @beater.max = 1;
    @beater.label = "Beater";

    @click = 2600;
    @click.widget = 1;
    @click.min = 400;
    @click.max = 9000;
    @click.label = "Beater Tone (Hz)";

    @drive = 1.6;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

# The gesture: 1 at the moment of contact and 0 a sweep later, so the
# head starts an octave up and settles at `Tune'.
node penv env::ad {
    a = 0;
    d = @sweep;
};

node head osc::simple {
    freq = @tune * (1 + penv->out);
    waveform = 0;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The felt: four milliseconds of it, which is a beater and not a snare.
node benv env::ad {
    a = 0;
    d = 4 ms;
};

node beater filt::svf {
    in = noise->out * benv->out;
    cutoff = @click;
    res = 0.3;
};

# The air in the drum, rung by the beater rather than by the tone: a
# shell answers the hit, not the note. High resonance and a band output,
# because what is wanted is one frequency ringing on past its excitation.
node shell filt::svf {
    in = beater->out_band * 0.5;
    cutoff = @shell;
    res = 0.97;
};

node env env::ad {
    a = 0.5 ms;
    d = @decay;
    p = ionode->velocity;
};

# Velocity into the beater as well as the level: a harder stroke is a
# brighter one, which is the difference between a drum and a fader.
node sum dist::saturate {
    in = head->out * 0.8 + shell->out_band * @body * 1.6 +
         beater->out_band * @beater * ionode->velocity * 1.4;
    factor = @drive;
};

node out mixer::mul {
    in0 = sum->out;
    in1 = env->out;
};

io ionode;
