# Kick Stack -- six partials over one pitch drop, each dying sooner than
# the last.
#
# An 808 kick is a sine and a pitch envelope, and `kick808.dsp' is that.
# A real drum is not: a struck head has a whole set of modes, they are
# not harmonically related, and the high ones lose their energy to the
# air and the shell long before the low one does. That is why a kick
# drum has a *click* and then a *tone* rather than one sound getting
# quieter, and it is what this file is built to do.
#
# SIX OSCILLATORS AT RATIOS OF ONE MOVING PITCH. `Drop' sweeps the
# fundamental from `Top' down to `Bottom' over `Pitch Decay'; the other
# five sit above it at compounding multiples of `Spread'. The ratios are
# deliberately not whole numbers -- at `Spread' 1.131 the stack is
# roughly a minor seventh apart and nothing lines up, which is what
# stops it sounding like an organ chord with a pitch bend on it.
#
# `Tighten' is the part worth having. Partial n's amplitude envelope
# decays in `Tighten' to the power n of the first one's, so at 0.9 the
# sixth partial is gone in a little over half the time the first takes,
# and at 0.6 it is a transient. Turn it to 1 and every partial decays
# together -- the whole character of a struck thing is in that number
# being under 1.
#
# Measured: how much of a band survives from the first 35 ms of the note
# into the 70-105 ms slice, low against high.
#
#     Tighten      60 Hz    300 Hz
#     1.0            2%        4%
#     0.9            3%        1%
#     0.6            4%        1%
#
# At 1 the high band outlasts the low one, which is what a bank of
# oscillators does and what no drum does. Under 1 it is the other way
# round, and that is the click-then-tone a struck head has.
#
# `filt::inkshape' across the stack, which is a gravity filter with a
# squashing term: `Shape' decides how hard a large step is flattened, so
# the attack -- which is one enormous step -- is squashed while the tail
# is not. A limiter would do that to the whole note; this does it to
# the transient only, and it is where the thump comes from.
#
# From `dsp/old/bd5.dsp' and its five siblings, 2004 -- six variants of
# this design, differing in tuning constants. `bd5' computed its fifth
# and sixth oscillators and then mixed the third and fourth twice, which
# `bd6' fixed; this is the fixed wiring, with the ladder of twenty
# `math::' nodes the originals needed written as arithmetic.

name "Kick Stack";
author "Misha Nasledov";
description "Six inharmonic partials over one pitch drop, the high ones dying first.";

    @top = 200;
    @top.widget = 1;
    @top.min = 60;
    @top.max = 600;
    @top.label = "Top (Hz)";

    @bottom = 17;
    @bottom.widget = 1;
    @bottom.min = 10;
    @bottom.max = 120;
    @bottom.label = "Bottom (Hz)";

    @drop = 80 ms;
    @drop.widget = 1;
    @drop.min = 2 ms;
    @drop.max = 600 ms;
    @drop.label = "Pitch Decay";

    @spread = 1.131;
    @spread.widget = 1;
    @spread.min = 1;
    @spread.max = 2.5;
    @spread.label = "Spread";

    @decay = 90 ms;
    @decay.widget = 1;
    @decay.min = 5 ms;
    @decay.max = 1000 ms;
    @decay.label = "Decay";

    @tighten = 0.9;
    @tighten.widget = 1;
    @tighten.min = 0.4;
    @tighten.max = 1;
    @tighten.label = "Tighten";

    @cutoff = 0.3;
    @cutoff.widget = 1;
    @cutoff.min = 0.02;
    @cutoff.max = 1;
    @cutoff.label = "Cutoff";

    @res = 0.95;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 1;
    @res.label = "Resonance";

    @shape = 2;
    @shape.widget = 1;
    @shape.min = 0;
    @shape.max = 6;
    @shape.label = "Shape";

    @wave = 5;
    @wave.widget = 1;
    @wave.min = 0;
    @wave.max = 5;
    @wave.step = 1;
    @wave.values = "Sine,Sawtooth,Square,Triangle,Half-circle,Parabola";
    @wave.label = "Wave";

    @level = 0.7;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    out0 = filt->out;
    out1 = filt->out;
    channels = 2;
    play = a0->play;
};

# The pitch drop. `env::ad' rather than an ADSR: a kick is a one-shot
# and there is nothing for a note-off to do to it. It runs longer than
# the amplitude envelope on purpose -- a pitch still falling when the
# note has gone is the part nobody hears and everybody misses.
node tone env::ad {
    a = 1 ms;
    d = @drop + @decay;
};

node pitch env::map {
    in = tone->out;
    inmin = 0;
    inmax = th_max;
    outmin = @bottom;
    outmax = @top;
};

# Six partials. Each one is `Spread' further up and `Tighten' shorter
# than the one below it, which is the whole instrument -- see the head.
node o0 osc::simple { freq = pitch->out;                    waveform = @wave; };
node o1 osc::simple { freq = pitch->out * @spread;          waveform = @wave; };
node o2 osc::simple { freq = pitch->out * pow(@spread, 2);  waveform = @wave; };
node o3 osc::simple { freq = pitch->out * pow(@spread, 3);  waveform = @wave; };
node o4 osc::simple { freq = pitch->out * pow(@spread, 4);  waveform = @wave; };
node o5 osc::simple { freq = pitch->out * pow(@spread, 5);  waveform = @wave; };

node a0 env::ad { a = 0; d = @decay;                   p = ionode->velocity; };
node a1 env::ad { a = 0; d = @decay * @tighten;        p = ionode->velocity; };
node a2 env::ad { a = 0; d = @decay * pow(@tighten,2); p = ionode->velocity; };
node a3 env::ad { a = 0; d = @decay * pow(@tighten,3); p = ionode->velocity; };
node a4 env::ad { a = 0; d = @decay * pow(@tighten,4); p = ionode->velocity; };
node a5 env::ad { a = 0; d = @decay * pow(@tighten,5); p = ionode->velocity; };

# Averaged rather than summed: six partials at full scale is six times
# full scale, and the limiter is not a mixer.
node stack math::mul {
    in0 = o0->out * a0->out +
          o1->out * a1->out +
          o2->out * a2->out +
          o3->out * a3->out +
          o4->out * a4->out +
          o5->out * a5->out;
    in1 = @level / 6;
};

node filt filt::inkshape {
    in = stack->out;
    cutoff = @cutoff;
    res = @res;
    shaper = @shape;
};

io ionode;
