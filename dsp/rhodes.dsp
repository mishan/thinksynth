# Rhodes -- a tine, a tonebar and a pickup, not a DX preset.
#
# epiano.dsp is how a DX7 draws this instrument: a 1:14 pair whose
# sidebands land high enough to read as a ring, over a 1:1 pair for the
# body. It is the sound of the record that replaced the Rhodes, and it
# is not the sound of the Rhodes.
#
# A real one is a steel tine struck by a hammer with a tonebar screwed
# beside it, both of them in front of an electromagnetic pickup. What
# that gives is:
#
#   A BARK AT THE FRONT and almost none after it. The hammer's contact
#   throws harmonics onto the tine which are gone in a couple of
#   hundred milliseconds. One 1:1 operator does that exactly -- a
#   modulator at the carrier's own frequency puts its sidebands on the
#   harmonic series, so the index is a brightness knob and nothing else
#   -- and it takes velocity, because how hard the hammer hits is the
#   only thing on the instrument that changes the timbre. The index
#   sits *low*: at a DX's settings this is a clang, and a Rhodes is a
#   bell only when it is hit hard.
#
#   THE TONEBAR, an octave up and gone in under half a second. The bar
#   is tuned an octave above the tine and rings in sympathy, which is
#   the chime that sits on top of the attack and is not part of the
#   note that follows.
#
#   A DECAY THAT DEPENDS ON THE NOTE. The bass tines are long and heavy
#   and ring for many seconds; the top two octaves are short stubs that
#   are gone almost at once. `Spread' is how much of that difference is
#   kept -- at 1, an octave down doubles the decay.
#
# THE TREMOLO IS STEREO, which is the other half of the instrument. A
# suitcase Rhodes does not turn the volume up and down: it pans the
# signal between two amplifiers, so one side rises as the other falls.
# That is one LFO and its inverse, half a cycle apart by construction,
# and it is why the effect sounds like the room moving rather than like
# a gate. `Depth' at 0 is a mono instrument.
#
# The phaser a piece wants on top of this is a channel effect -- the
# player plugged one in -- and belongs on the instrument as
# `effect "fx/phaser.dsp"'.

name "Rhodes";
author "Misha Nasledov";
description "A tine with a hammer bark, a tonebar chime and a stereo tremolo: the electric piano.";
category "Keys";

    @bark = 2.6;
    @bark.widget = 1;
    @bark.min = 0;
    @bark.max = 8;
    @bark.label = "Bark";

    @bd = 180 ms;
    @bd.widget = 1;
    @bd.min = 10ms;
    @bd.max = 1500ms;
    @bd.label = "Bark Decay";

    @bar = 0.35;
    @bar.widget = 1;
    @bar.min = 0;
    @bar.max = 1;
    @bar.label = "Tonebar";

    @bard = 320 ms;
    @bard.widget = 1;
    @bard.min = 20ms;
    @bard.max = 2000ms;
    @bard.label = "Tonebar Decay";

    @decay = 2200 ms;
    @decay.widget = 1;
    @decay.min = 200ms;
    @decay.max = 9000ms;
    @decay.label = "Decay";

    @spread = 1;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 2;
    @spread.label = "Decay Spread";

    @rate = 5.2;
    @rate.widget = 1;
    @rate.min = 0.1;
    @rate.max = 12;
    @rate.label = "Tremolo (Hz)";

    @depth = 0.7;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 1;
    @depth.label = "Tremolo Depth";

    @a = 3 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 200ms;
    @a.label = "Attack";

    @r = 320 ms;
    @r.widget = 1;
    @r.min = 10ms;
    @r.max = 3000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = left->out;
    out1 = right->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The hammer. A 1:1 modulator, so every sideband it makes lands on a
# harmonic of the note and the index reads as brightness; velocity is on
# the index rather than on the level, which is what makes a hard note
# bark instead of merely being louder.
node hammer osc::fmop {
    freq = freq->out;
    ratio = 1;
};

node benv env::ad {
    a = 0;
    d = @bd;
};

node tine osc::fmop {
    freq = freq->out;
    ratio = 1;
    mod = hammer->out;
    index = @bark * benv->out * ionode->velocity;
};

# The tonebar: an octave up, and gone before the note has settled.
node bar osc::simple {
    freq = freq->out * 2;
    waveform = 0;
};

node barenv env::ad {
    a = 0;
    d = @bard;
    p = ionode->velocity;
};

# A long tine and a short one. `Spread' at 1 doubles the decay an octave
# below middle C and halves it an octave above, which is roughly the
# ratio across a real keyboard.
node env env::adsr {
    a = @a;
    d = 30 ms;
    s = th_max;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

# The tine's own decay, which is the note's: the amp envelope above only
# shapes the ends, because a key held down on a Rhodes does not sustain.
node fall env::ad {
    a = 1 ms;
    d = @decay * exp2(@spread * (60 - ionode->note) / 12);
    p = ionode->velocity;
};

node voice math::add {
    in0 = tine->out * fall->out * 0.8;
    in1 = bar->out * barenv->out * @bar * 0.5;
};

node amp mixer::mul {
    in0 = voice->out;
    in1 = env->out;
};

# One LFO and its inverse: a suitcase Rhodes pans between two amplifiers
# rather than turning the level up and down, so the sides are half a
# cycle apart by construction and the sum of them does not move.
node lfo osc::simple {
    freq = @rate;
    waveform = 0;
    amp = 1;
};

node left mixer::mul {
    in0 = amp->out;
    in1 = 1 - @depth * 0.5 + lfo->out * @depth * 0.5;
};

node right mixer::mul {
    in0 = amp->out;
    in1 = 1 - @depth * 0.5 - lfo->out * @depth * 0.5;
};

io ionode;
