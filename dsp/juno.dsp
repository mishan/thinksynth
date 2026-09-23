# Juno -- one oscillator, a sub, a filter and the chorus button.
#
# The pad under every synthpop chorus of the decade, and the surprise is
# how little is in it. One digitally-divided pulse per voice. One square
# an octave below it. A four-pole low-pass with an envelope. A chorus
# that was a single BBD line with a fixed rate and two buttons, on or
# off. No second oscillator, no detune, no sync, no ring modulator --
# every other polysynth of the period had more and none of them sounds
# like this.
#
# THE PULSE WIDTH IS WHERE THE MOVEMENT COMES FROM. With one oscillator
# and nothing to beat against, a held chord would sit perfectly still,
# and a pad that sits still is a drone. A triangle LFO on the pulse
# width fixes that for free: as the width moves, the harmonics move in
# and out of phase with each other, and the note breathes without its
# pitch moving at all. `PWM Rate' at a fraction of a hertz is the Juno's
# own; faster is a vibrato it never had.
#
# THE SUB IS A SQUARE AND NOT A SINE, which matters more than it sounds
# like it should. A sine an octave down adds weight; a square an octave
# down adds a whole odd-harmonic series under the note, and *that* is
# what lets one oscillator sound like a stack. The original's sub was a
# divider output and had no choice about being a square.
#
# CHORUS INSIDE THE VOICE, for the reason strings.dsp gives at length:
# the voices here are in tune to the sample, so a chorus on the channel
# sum moves the whole chord together and what comes out is one wobbling
# instrument. Two taps at half a hertz, which is the Juno's slow button;
# for the fast one, turn `Chorus Rate' up.

name "Juno";
author "Misha Nasledov";
description "A pulse with a moving width, a sub square, and a chorus in the voice: the Juno pad.";
category "Strings and pads";

    @pw = 0.5;
    @pw.widget = 1;
    @pw.min = 0.05;
    @pw.max = 0.95;
    @pw.label = "Pulse Width";

    @pwdepth = 0.3;
    @pwdepth.widget = 1;
    @pwdepth.min = 0;
    @pwdepth.max = 0.45;
    @pwdepth.label = "PWM Depth";

    @pwrate = 0.4;
    @pwrate.widget = 1;
    @pwrate.min = 0.05;
    @pwrate.max = 8;
    @pwrate.label = "PWM Rate (Hz)";

    @sub = 0.45;
    @sub.widget = 1;
    @sub.min = 0;
    @sub.max = 1;
    @sub.label = "Sub Octave";

    @cutoff = 700;
    @cutoff.widget = 1;
    @cutoff.min = 80;
    @cutoff.max = 8000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 2600;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 10000;
    @depth.label = "Envelope Depth (Hz)";

    @res = 0.5;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";

    @fa = 180 ms;
    @fa.widget = 1;
    @fa.min = 0;
    @fa.max = 3000ms;
    @fa.label = "Filter Attack";

    @fd = 900 ms;
    @fd.widget = 1;
    @fd.min = 10ms;
    @fd.max = 6000ms;
    @fd.label = "Filter Decay";

    @fs = 0.35;
    @fs.widget = 1;
    @fs.min = 0.02;
    @fs.max = 1;
    @fs.label = "Filter Sustain";

    @chorus = 0.5;
    @chorus.widget = 1;
    @chorus.min = 0;
    @chorus.max = 1;
    @chorus.label = "Chorus";

    @chrate = 0.5;
    @chrate.widget = 1;
    @chrate.min = 0.1;
    @chrate.max = 8;
    @chrate.label = "Chorus Rate (Hz)";

    @a = 140 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 4000ms;
    @a.label = "Attack";

    @d = 700 ms;
    @d.widget = 1;
    @d.min = 20ms;
    @d.max = 6000ms;
    @d.label = "Decay";

    # See strings.dsp: a sustain of exactly zero ends the note and the
    # held key then restarts it.
    @s = 0.7;
    @s.widget = 1;
    @s.min = 0.02;
    @s.max = 1;
    @s.label = "Sustain";

    @r = 600 ms;
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

# Triangle rather than sine: the width should spend its time moving and
# not sitting at the extremes, and a triangle is the wave that does.
node pwlfo osc::simple {
    freq = @pwrate;
    waveform = 3;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 2;
    pw = @pw + pwlfo->out * @pwdepth;
};

node sub osc::simple {
    freq = freq->out * 0.5;
    waveform = 2;
};

node fenv env::adsr {
    a = @fa;
    d = @fd;
    s = @fs;
    r = @r;
    trigger = ionode->trigger;
};

# Where the sub gives up. The same clamp dsp/bass.dsp carries, for the
# same reason: the sub plays an octave below the note, so in the bottom
# octave it plays below hearing -- at MIDI 24 the note is 32.7 Hz and
# the sub is 16.4 Hz, which is a flutter rather than weight. It fades
# out below 120 Hz and is gone by 55 Hz, and above 120 Hz nothing here
# changes.
#
# Unlike bass.dsp the pulse is not scaled against the sub here -- the
# original's sub was added to a pulse that stayed where it was -- so a
# low note loses the sub's share of the level rather than having it
# handed back. That is the Juno's arithmetic and is left alone.
node subamt math::clamp {
    in = (freq->out - 55) / 65;
    lo = 0;
    hi = 1;
};

node filt filt::svf {
    in = osc->out * 0.5 + sub->out * 0.5 * @sub * subamt->out;
    cutoff = @cutoff + fenv->out * @depth * ionode->velocity;
    res = @res;
};

node ch delay::chorus {
    in = filt->out_low;
    rate = @chrate;
    depth = 2.5 ms;
    delay = 10 ms;
    taps = 2;
    mix = @chorus;
    phase = 0;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = ch->out * 0.85;
    in1 = env->out;
};

io ionode;
