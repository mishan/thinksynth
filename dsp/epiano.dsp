# E.Piano -- the tine electric piano, as a DX draws one.
#
# Two pairs of operators, each pair a modulator at some ratio into a
# carrier at the voice's own pitch, and the whole sound is in how
# differently their two indices decay.
#
# The BODY is a 1:1 pair. A modulator at the carrier's own frequency
# puts its sidebands on the harmonics, so the pair is one note getting
# duller as `Body Decay' runs out -- a struck string, or near enough.
#
# The TINE is 1:14. Fourteen is high enough that the sidebands land in
# the range a listener hears as a ring rather than as a pitch, and
# `Tine Decay' is a few hundred milliseconds, so the ring is gone before
# the body is. That clang at the front of the note and nothing after it
# is what a Rhodes hammer does to a tine, which is what the preset was
# named for.
#
# VELOCITY GOES TO THE INDICES, which is the whole trick and the reason
# this cannot be built with a filter. An index is how far the modulator
# pushes the carrier's phase, so more of it is more sidebands and a
# brighter note -- and on a real one, hitting the key harder raises the
# modulators' output levels rather than the carriers'. The amplitude
# only moves half as far (`0.5 + velocity * 0.5'), so a soft note is a
# little quieter and a lot rounder, the way a tine is.
#
# The vibrato is slow and shallow and arrives late: an electric piano has
# no vibrato of its own, and this stands in for the one the player's
# amplifier had. The stereo chorus a DX piano is always heard through is
# a channel effect and not a node here -- `effect "fx/chorus.dsp"' on
# the instrument in the piece.

name "FM E.Piano";
author "Misha Nasledov";
description "Two operator pairs, a tine over a body: the DX electric piano.";

    @body = 2.2;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 8;
    @body.label = "Body Index";

    @bodyd = 900 ms;
    @bodyd.widget = 1;
    @bodyd.min = 20ms;
    @bodyd.max = 4000ms;
    @bodyd.label = "Body Decay";

    @tine = 4.5;
    @tine.widget = 1;
    @tine.min = 0;
    @tine.max = 12;
    @tine.label = "Tine Index";

    @tined = 240 ms;
    @tined.widget = 1;
    @tined.min = 10ms;
    @tined.max = 2000ms;
    @tined.label = "Tine Decay";

    # Fourteen is the preset's. Lower is a reedier electric piano and
    # higher is a bell, and both are one slider away.
    @tineratio = 14;
    @tineratio.widget = 1;
    @tineratio.min = 1;
    @tineratio.max = 20;
    @tineratio.label = "Tine Ratio";

    @mix = 0.4;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Tine Level";

    # The body modulator's own output back into its phase. A little of
    # it puts the odd harmonics a sine pair has no way to reach into the
    # bottom of the note, which is where a Rhodes gets its growl.
    @grit = 0.2;
    @grit.widget = 1;
    @grit.min = 0;
    @grit.max = 1;
    @grit.label = "Growl";

    @vib = 6;
    @vib.widget = 1;
    @vib.min = 0;
    @vib.max = 50;
    @vib.label = "Vibrato (cents)";

    @vibrate = 4.5;
    @vibrate.widget = 1;
    @vibrate.min = 0.5;
    @vibrate.max = 12;
    @vibrate.label = "Vibrato Rate (Hz)";

    @vibdelay = 500 ms;
    @vibdelay.widget = 1;
    @vibdelay.min = 0;
    @vibdelay.max = 2000ms;
    @vibdelay.label = "Vibrato Delay";

    @d = 2600 ms;
    @d.widget = 1;
    @d.min = 100ms;
    @d.max = 8000ms;
    @d.label = "Decay";

    # The bottom of the slider is a little above zero on purpose: an
    # env::adsr whose decay arrives at a sustain of exactly nothing ends
    # the note, and a still-held key then reads as a retrigger and starts
    # it again. Everything above this is an envelope; zero is a loop.
    @s = 0.22;
    @s.widget = 1;
    @s.min = 0.02;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 280 ms;
    @r.widget = 1;
    @r.min = 10ms;
    @r.max = 3000ms;
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

node vib misc::vibrato {
    in = freq->out;
    rate = @vibrate;
    depth = @vib;
    delay = @vibdelay;
    rise = @vibdelay * 0.5;
};

# The body. Its index decays over the best part of a second, so the
# note keeps a little of its edge for as long as it is held.
#
# env::ad rather than an ADSR with no sustain, and the tine below is
# the same. An env::adsr that reaches the end of its decay with `s = 0'
# ends the note -- and then sees a trigger still held and starts over,
# which for an envelope wired to the amplitude is the voice being
# retired and for one wired to an index is a loop. AD has no sustain to
# leave out: it runs once when the voice starts and stays at zero.
node bodyidx env::ad {
    a = 0;
    d = @bodyd;
};

node bodymod osc::fmop {
    freq = vib->out;
    ratio = 1;
    feedback = @grit;
};

node bodycar osc::fmop {
    freq = vib->out;
    ratio = 1;
    mod = bodymod->out;
    index = bodyidx->out * @body * ionode->velocity;
};

# And the tine, which is over before the body has started to settle.
node tineidx env::ad {
    a = 0;
    d = @tined;
};

node tinemod osc::fmop {
    freq = vib->out;
    ratio = @tineratio;
};

node tinecar osc::fmop {
    freq = vib->out;
    ratio = 1;
    mod = tinemod->out;
    index = tineidx->out * @tine * ionode->velocity;
};

# Half the velocity range, against the whole of it on the indices
# above: a light touch is rounder, not just quieter.
node env env::adsr {
    a = 2 ms;
    d = @d;
    s = @s * (0.5 + ionode->velocity * 0.5);
    r = @r;
    p = 0.5 + ionode->velocity * 0.5;
    trigger = ionode->trigger;
};

# The 0.8 is the gain staging the rest of the corpus sits at: a single
# voice peaks around six tenths, so a chord of four is the limiter's
# problem and not a sign flip.
node vca mixer::mul {
    in0 = (bodycar->out * (1 - @mix) + tinecar->out * @mix) * 0.8;
    in1 = env->out;
};

io ionode;
