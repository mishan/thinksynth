# Orchestra Hit -- one recording, played at whatever pitch you like.
#
# The sound of the decade's second half, and an accident of the
# Fairlight's library: a orchestra playing one short chord, sampled, and
# then played from a keyboard so that the whole orchestra moves with the
# note. Nobody had heard that before, because an orchestra cannot be
# transposed by pressing a key, and for about four years everybody used
# it.
#
# WHAT IT DEMONSTRATES, which is why it is in the tree: this is the one
# instrument on the list that a graph genuinely cannot be. A hit is a
# dozen instruments with different attacks, different noise and different
# tuning arriving within a few milliseconds of each other, and its
# character is precisely that it is not coherent. An oscillator stack can
# be made loud and bright; it cannot be made to have been recorded.
#
# The wav is the tree's own -- `stab' and `brass' hitting C4 together,
# rendered by scripts/makekit.sh. Two graphs at unison rather than one,
# because a hit needs the disagreement: the stab's filter and the brass's
# buzz start at different times and drift apart over the first hundred
# milliseconds, and that beating is most of what the ear takes for an
# orchestra.
#
# `root' IS 261.63 AND STAYS THERE, because that is the note the file was
# recorded at. Everything else follows: a hit played an octave up reads
# at twice the speed, which is an octave up *and half as long*, which is
# exactly what a sampler of this period did and is the reason an
# eighties hit gets shorter and more comical as it climbs. A piece that
# wants the length back plays it lower and transposes the part.
#
# THE FILTER ENVELOPE IS THE PLAYER. A recording has one attack, and it
# is whatever the microphone heard; a graph can have as many as it likes.
# `Snap' shuts the low-pass down over a few tens of milliseconds, which
# turns one recording into a hit that is stabbed rather than one that is
# held -- the difference between the Fairlight's sample and what a record
# actually used it for.
#
# THE NOTE ENDS AT WHICHEVER FINISHES FIRST, the file or the envelope.
# `play' from the sampler alone would hold a note through a decay that
# had already reached zero; the envelope alone would cut a hit played an
# octave down in half. `math::min' of the two is the only answer that is
# right at both ends of the keyboard.

name "Orchestra Hit";
author "Misha Nasledov";
description "A sampled orchestra chord, transposed and stabbed: the Fairlight hit.";
category "Leads and stabs";

    @cutoff = 700;
    @cutoff.widget = 1;
    @cutoff.min = 100;
    @cutoff.max = 12000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 8000;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 16000;
    @depth.label = "Envelope Depth (Hz)";

    @snap = 90 ms;
    @snap.widget = 1;
    @snap.min = 2ms;
    @snap.max = 1500ms;
    @snap.label = "Snap";

    @res = 0.2;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.9;
    @res.label = "Resonance";

    @start = 0;
    @start.widget = 1;
    @start.min = 0;
    @start.max = 8000;
    @start.label = "Start (samples)";

    @a = 0.5 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 200ms;
    @a.label = "Attack";

    @d = 900 ms;
    @d.widget = 1;
    @d.min = 20ms;
    @d.max = 4000ms;
    @d.label = "Decay";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = playing->out;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node smp osc::sample {
    file = "orchhit.wav";
    freq = freq->out;
    root = 261.63;
    start = @start;
    trigger = ionode->trigger;
};

# Straight to the top and down over `Snap'. A hit has no sustain: the
# orchestra stopped.
node fenv env::ad {
    a = 0;
    d = @snap;
};

# Velocity on the filter as well as the level, which is what makes a
# quiet hit a duller one rather than a smaller copy of the same sound.
node filt filt::svf {
    in = smp->out;
    cutoff = @cutoff + fenv->out * @depth * ionode->velocity;
    res = @res;
};

node env env::ad {
    a = @a;
    d = @d;
    p = ionode->velocity;
};

node out mixer::mul {
    in0 = filt->out_low * 0.9;
    in1 = env->out;
};

# Whichever runs out first -- see the head.
node playing math::min {
    in0 = smp->play;
    in1 = env->play;
};

io ionode;
