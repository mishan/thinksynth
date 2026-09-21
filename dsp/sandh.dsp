# S&H -- a pulse whose width is redrawn at the top of every cycle.
#
# Sample-and-hold is the oldest randomiser on a modular: a noise source
# and a clock, and whatever the noise happened to be when the clock
# ticked is held until the next tick. Point it at a filter and it is the
# burbling sequence on every seventies record. Point it at a pulse
# width, which is what this does, and the sound is stranger and less
# familiar -- the pitch never moves, and the timbre steps somewhere new
# on a grid.
#
# `misc::latch' is the sample-and-hold: it tracks its input while
# `latch' is above zero and holds when it drops. `osc::simple' puts out
# a 1 at the start of each cycle on `sync', so a clock oscillator's sync
# is a tick.
#
# THE CLOCK IS ITS OWN OSCILLATOR AND NOT THE VOICE'S, which is the one
# change from `dsp/old/randompw.dsp' that this came from. That file
# latched on the sync of the very oscillator whose width it was setting,
# which is a cycle in the graph: the walk clears each node's recalc flag
# before it recurses, so the loop closed through a whole window and the
# file sounded like whatever buffer size the audio device asked for --
# measured, it renders differently at 1024 samples and at 2048, first
# diverging 1760 samples in. A second oscillator reading the same
# frequency ticks at the same instant and reads nothing downstream of
# itself, so the graph is acyclic and the sound is the device's business
# no longer.
#
# `Rate' is that clock as a ratio of the note. At 1 the width is redrawn
# once a cycle, which is fast enough that it reads as a timbre rather
# than as a sequence -- a rough, vocal buzz. Divide it down and the
# steps separate out until each one is audible, and somewhere around an
# eighth it stops being a tone colour and starts being a part.
#
# `Spread' is how far apart the two widths it chooses between are.
# Narrow is a tremble; at the extremes a pulse wave is nearly silent at
# one end of its travel, so wide is a stutter with gaps in it.
#
# `filt::rds' rather than a proper low-pass. It is a one-pole that
# scales the *step* between one sample and the next by the square of its
# resonance, which means it does not ring, it exaggerates. On a signal
# that moves in jumps, that is the knob that makes the jumps audible,
# and it is why this file has always used it.
#
# THE ORIGINAL HAD NO ENVELOPE. `play = 1' in the io node, so the engine
# was never told the note ended and a voice sounded until `poly' stole
# it. This one has an ADSR and the note stops.

name "S&H";
author "Misha Nasledov";
description "A pulse width redrawn by a sample-and-hold on every clock tick.";

    @rate = 1;
    @rate.widget = 1;
    @rate.min = 0.03;
    @rate.max = 4;
    @rate.label = "Rate";

    @centre = 0.45;
    @centre.widget = 1;
    @centre.min = 0.1;
    @centre.max = 0.9;
    @centre.label = "Centre";

    @spread = 0.3;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 0.8;
    @spread.label = "Spread";

    @wave = 2;
    @wave.widget = 1;
    @wave.min = 0;
    @wave.max = 5;
    @wave.step = 1;
    @wave.values = "Sine,Sawtooth,Square,Triangle,Half-circle,Parabola";
    @wave.label = "Wave";

    @cutoff = 0.6;
    @cutoff.widget = 1;
    @cutoff.min = 0.05;
    @cutoff.max = 1;
    @cutoff.label = "Cutoff";

    @res = 0.9;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 1;
    @res.label = "Edge";

    @a = 4 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @d = 400 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 4000 ms;
    @d.label = "Decay";

    @s = 70%;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 250 ms;
    @r.widget = 1;
    @r.min = 2 ms;
    @r.max = 4000 ms;
    @r.label = "Release";

    @level = 0.6;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 2;
    @level.label = "Level";

node ionode {
    out0 = vca->out;
    out1 = vca->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# A fresh number every sample. `sample' is left at 0 -- how long a value
# is held is the latch's business, not the noise's.
node noise osc::static {
    sample = 0;
};

# The clock. Nothing reads its output; it is here for `sync', and for
# being upstream of everything rather than downstream of the voice.
node clock osc::simple {
    freq = freq->out * @rate;
    waveform = 1;
};

node width misc::latch {
    in = @centre + noise->out * @spread * 0.5;
    latch = clock->sync;
};

node osc osc::simple {
    freq = freq->out;
    waveform = @wave;
    pw = width->out;
};

node filt filt::rds {
    in = osc->out;
    cutoff = @cutoff;
    res = @res;
};

node vca mixer::mul {
    in0 = filt->out * @level;
    in1 = env->out;
};

io ionode;
