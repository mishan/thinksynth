# Kit Tom -- one graph, three drums, chosen by the note.
#
# The other drums in this kit ignore the note number, because a kick is
# a kick and a snare is a snare. A tom is not one drum: a kit has a rack
# pair and a floor tom, a fill runs across them, and writing three
# copies of this file with three tunings in them would be three files to
# keep in step. So the pitch comes from the note -- `misc::midi2freq' on
# `ionode->note' -- and one channel is the whole set. C2 is a floor tom,
# the octave above it is the small rack tom, and a `gen::euclid' with a
# pool of three notes is a fill.
#
# WHAT A TOM IS: a head whose pitch falls a fourth in the first fifty
# milliseconds -- less than a kick's octave, because a tom's head is
# tuned rather than slack -- over a shell an octave below it that rings
# longer than the head does. The low ring is why a tom fill fills: a
# snare stops when the stroke stops, and a tom keeps going.
#
# `Tune' offsets the whole set in semitones, so a piece can move the
# toms without rewriting the part. The decay scales with the drum: a
# floor tom rings twice as long as a small rack tom, which is
# `Size' -- one at the reference note and up as the pitch falls.
#
# The stick is an env::ad click and the shell is a filt::svf band; see
# rim808.dsp for why neither is the `impulse::' node or the
# `filt::resonator' the shape suggests.

name "Kit Tom";
author "Misha Nasledov";
description "A head that falls a fourth over a shell that rings, tuned by the note: the toms.";
category "Drums";

    @tune = 0;
    @tune.widget = 1;
    @tune.min = -12;
    @tune.max = 12;
    @tune.step = 1;
    @tune.label = "Tune (semitones)";

    @sweep = 50 ms;
    @sweep.widget = 1;
    @sweep.min = 5ms;
    @sweep.max = 300ms;
    @sweep.label = "Head Sweep";

    @bend = 5;
    @bend.widget = 1;
    @bend.min = 0;
    @bend.max = 12;
    @bend.label = "Head Bend (semitones)";

    @decay = 420 ms;
    @decay.widget = 1;
    @decay.min = 40ms;
    @decay.max = 3000ms;
    @decay.label = "Decay";

    @size = 1;
    @size.widget = 1;
    @size.min = 0;
    @size.max = 2;
    @size.label = "Size";

    @shell = 0.6;
    @shell.widget = 1;
    @shell.min = 0;
    @shell.max = 1.5;
    @shell.label = "Shell";

    @stick = 0.35;
    @stick.widget = 1;
    @stick.min = 0;
    @stick.max = 1.5;
    @stick.label = "Stick";

    @drive = 1.4;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

node ionode {
    channels = 2;

    # A fill leaves each drum ringing while the next is struck.
    poly = 4;

    out0 = sum->out;
    out1 = sum->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note + @tune;
};

# 1 at the stroke, 0 a sweep later, so the head starts `Head Bend'
# semitones sharp and settles on the note.
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

# Two milliseconds of stick, which is what excites the shell below.
node tip env::ad {
    a = 0;
    d = 2 ms;
};

node hit filt::svf {
    in = noise->out * tip->out;
    cutoff = pitch->out * 6;
    res = 0.3;
};

# The shell, an octave under the head and rung by the stick rather than
# played by it. It is what is still sounding when the head has gone.
node body filt::svf {
    in = hit->out_band * 0.6;
    cutoff = pitch->out * 0.5;
    res = 0.97;
};

# A big drum rings longer. `Size' at 1 doubles the decay an octave below
# C3 and halves it an octave above, which is how a rack tom and a floor
# tom differ once they are both tuned. C3 rather than middle C because
# that is where a rack tom sits and this is a tom.
node env env::ad {
    a = 0.5 ms;
    d = @decay * exp2(@size * (48 - ionode->note - @tune) / 12);
    p = ionode->velocity;
};

node senv env::ad {
    a = 0.5 ms;
    d = @decay * 1.6 * exp2(@size * (48 - ionode->note - @tune) / 12);
    p = ionode->velocity;
};

node sum dist::saturate {
    in = head->out * 0.75 * env->out +
         body->out_band * @shell * senv->out * 1.4 +
         hit->out_high * @stick * ionode->velocity;
    factor = @drive;
};

io ionode;
