# Timbale -- a shallow steel drum, played on the head and on the rim.
#
# A timbale is a tom's arrangement with none of a tom's wood: a shallow
# steel shell, a single head tuned high and tight, and no bottom head at
# all. What that changes is the proportion. A tom is mostly tone with a
# stick on the front; a timbale is half noise, because a thin head on
# steel rings a great deal of it, and the ring is metal rather than air.
#
# THE RIM IS THE OTHER INSTRUMENT. A player striking the shell's edge
# gets a crack with no pitch in it, and the pattern alternates the two:
# that is the cascara, and it is what a timbale part is made of. Here it
# is one graph and velocity picks which -- above `Rim' the stroke is on
# the edge, below it on the head, with the head's tone falling away as
# the rim's crack comes up. Squared, so the middle of the range is not
# permanently half a rim shot.
#
# The pitch comes from the note, so one channel is the macho and the
# hembra -- the big drum and the small one -- and a fill runs across
# both. Two files with two tunings would be two files to keep in step.
#
# The head's bend is small: a fourth is a tom, and a timbale's head is
# tight enough that it hardly moves at all.

name "Timbale";
author "Misha Nasledov";
description "A high steel drum with a rim shot velocity picks: the timbale.";
category "Drums";

    @tune = 0;
    @tune.widget = 1;
    @tune.min = -12;
    @tune.max = 12;
    @tune.step = 1;
    @tune.label = "Tune (semitones)";

    @bend = 2;
    @bend.widget = 1;
    @bend.min = 0;
    @bend.max = 12;
    @bend.label = "Head Bend (semitones)";

    @sweep = 25 ms;
    @sweep.widget = 1;
    @sweep.min = 2ms;
    @sweep.max = 200ms;
    @sweep.label = "Head Sweep";

    @decay = 180 ms;
    @decay.widget = 1;
    @decay.min = 20ms;
    @decay.max = 1500ms;
    @decay.label = "Decay";

    @steel = 0.8;
    @steel.widget = 1;
    @steel.min = 0;
    @steel.max = 2;
    @steel.label = "Steel";

    @ring = 2600;
    @ring.widget = 1;
    @ring.min = 600;
    @ring.max = 9000;
    @ring.label = "Shell (Hz)";

    @rim = 0.75;
    @rim.widget = 1;
    @rim.min = 0.1;
    @rim.max = 1;
    @rim.label = "Rim Threshold";

    @crack = 1.1;
    @crack.widget = 1;
    @crack.min = 0;
    @crack.max = 2;
    @crack.label = "Rim Level";

node ionode {
    channels = 2;

    # A cascara leaves each stroke ringing under the next.
    poly = 4;

    out0 = out->out;
    out1 = out->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note + @tune;
};

node penv env::ad {
    a = 0;
    d = @sweep;
};

node head osc::simple {
    freq = pitch->out * exp2(@bend * penv->out / 12);
    waveform = 0;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node stick env::ad {
    a = 0;
    d = 2 ms;
};

# The shell: steel, so a band well above the head and rung by the stick
# rather than tuned with it. It is most of what a timbale is.
node shell filt::svf {
    in = noise->out * stick->out;
    cutoff = @ring;
    res = 0.93;
};

node env env::ad {
    a = 0.3 ms;
    d = @decay;
    p = ionode->velocity;
};

node senv env::ad {
    a = 0.3 ms;
    d = @decay * 1.3;
    p = ionode->velocity;
};

# Nothing below the threshold and the rest of the range stretched across
# the whole of the rim, squared so the crack arrives late and hard.
node edge math::clamp {
    in = (ionode->velocity - @rim) / (1 - @rim);
    lo = 0;
    hi = 1;
};

node how math::mul {
    in0 = edge->out;
    in1 = edge->out;
};

node crack filt::svf {
    in = noise->out * stick->out;
    cutoff = 5200;
    res = 0.4;
};

node cenv env::ad {
    a = 0;
    d = 45 ms;
    p = ionode->velocity;
};

node out math::add {
    in0 = (head->out * 0.7 * env->out +
           shell->out_band * @steel * senv->out) * (1 - how->out * 0.6);
    in1 = crack->out_high * cenv->out * how->out * @crack;
};

io ionode;
