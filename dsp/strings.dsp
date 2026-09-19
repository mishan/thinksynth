# Strings -- the string machine, and the ensemble is the instrument.
#
# A Solina, an RS-202, a Logan: a divide-down organ with a sawtooth on
# every key and no filter worth the name, which on its own is a thin and
# unpleasant sound. What made it a string machine was the box after it --
# a bucket-brigade chorus with three taps on one line, each moved by its
# own phase of a slow LFO, mixed back against the dry. That is where the
# whole of the sound lives, and it is the reason the chorus here is
# inside the voice rather than on the channel.
#
# INSIDE THE VOICE, WHICH IS UNUSUAL AND DELIBERATE. `fx/chorus.dsp'
# exists and is the right answer for most things: chorusing a channel's
# sum is cheaper and gives a chord one coherent movement. A string
# machine is the case where that is wrong. On a real one every key had
# its own divider and the ensemble sat after the lot of them, but the
# keys were never in tune with each other to begin with -- so what the
# ensemble worked on was already a crowd. Here the voices *are* in tune,
# to the sample, and a chorus on their sum moves all of them together:
# one detuned instrument rather than a section. A chorus per voice is
# what puts each note on its own wobble, which is what a section is.
#
# TWO CHORUS NODES AT TWO RATES, in series, three taps each. The Solina
# had two LFOs -- about a half hertz and about six -- and used both at
# once. The slow one is the drift that makes a held chord move; the fast
# one is the shimmer that keeps a single note from sounding like one
# oscillator. Three taps apiece because the original had three, spread
# across half a cycle so they do not cancel -- see the head of
# plugins/delay/chorus.cpp for why a whole cycle would.
#
# The two saws are a few cents apart, which is the detuning *inside* one
# key that the dividers never had and that the ensemble is standing in
# for. The filter is a gentle low-pass with no envelope on it: a string
# machine has a tone control and not a VCF, and putting an envelope here
# would make it a pad synthesizer, which is `juno.dsp'.
#
# Slow in, slow out, and a sustain that holds -- the attack and release
# are the only expression the instrument has.

name "Strings";
author "Misha Nasledov";
description "Two saws through a three-tap ensemble at two rates: the string machine.";

    @detune = 7;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 40;
    @detune.label = "Detune (cents)";

    @cutoff = 3400;
    @cutoff.widget = 1;
    @cutoff.min = 400;
    @cutoff.max = 12000;
    @cutoff.label = "Tone (Hz)";

    @res = 0.15;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @slow = 0.6;
    @slow.widget = 1;
    @slow.min = 0.05;
    @slow.max = 3;
    @slow.label = "Drift Rate (Hz)";

    @slowdepth = 4 ms;
    @slowdepth.widget = 1;
    @slowdepth.min = 0.1ms;
    @slowdepth.max = 12ms;
    @slowdepth.label = "Drift Depth";

    @fast = 6;
    @fast.widget = 1;
    @fast.min = 1;
    @fast.max = 10;
    @fast.label = "Shimmer Rate (Hz)";

    @fastdepth = 0.5 ms;
    @fastdepth.widget = 1;
    @fastdepth.min = 0.05ms;
    @fastdepth.max = 4ms;
    @fastdepth.label = "Shimmer Depth";

    @ensemble = 0.6;
    @ensemble.widget = 1;
    @ensemble.min = 0;
    @ensemble.max = 1;
    @ensemble.label = "Ensemble";

    @a = 220 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 3000ms;
    @a.label = "Attack";

    @d = 600 ms;
    @d.widget = 1;
    @d.min = 20ms;
    @d.max = 4000ms;
    @d.label = "Decay";

    # Just off zero: an env::adsr whose decay arrives at a sustain of
    # exactly nothing ends the note, and a still-held key then reads as
    # a retrigger and starts it again.
    @s = 0.8;
    @s.widget = 1;
    @s.min = 0.02;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 700 ms;
    @r.widget = 1;
    @r.min = 10ms;
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

node osc2 osc::simple {
    freq = freq->out * exp2(@detune / 1200);
    waveform = 1;
};

node tone filt::svf {
    in = osc1->out * 0.5 + osc2->out * 0.5;
    cutoff = @cutoff;
    res = @res;
};

# The ensemble: the slow one first, then the fast one over it. Delay
# short enough to be a chorus and not a doubler, and both at full mix,
# since `Ensemble' below is where the dry signal comes back in.
node drift delay::chorus {
    in = tone->out_low;
    rate = @slow;
    depth = @slowdepth;
    delay = 9 ms;
    taps = 3;
    mix = 1;
    phase = 0;
};

node shimmer delay::chorus {
    in = drift->out;
    rate = @fast;
    depth = @fastdepth;
    delay = 4 ms;
    taps = 3;
    mix = 1;
    phase = 0.25;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# `Ensemble' against the dry filter output, which is the beating the
# whole instrument is made of: at 0 it is a pair of saws and at 1 it is
# three detuned copies with nothing to disagree with.
node vca mixer::mul {
    in0 = tone->out_low * (1 - @ensemble) * 0.8 + shimmer->out * @ensemble * 0.8;
    in1 = env->out;
};

io ionode;
