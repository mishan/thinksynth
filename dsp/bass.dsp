# Bass -- one voice, and notes that overlap slide into each other.
#
# `mono = 1' on the io node says a note arriving while another is held
# retunes the voice that is sounding rather than starting a second one.
# The voice keeps its envelopes, so a slide does not re-attack, and it
# keeps its graph state -- which is what makes the `glide' below work:
# misc::slew is lagging a frequency that moves under it, and it only
# moves because the voice outlived the note that started it.
#
# The rule is the whole of it: overlap is a slide, a gap is a new note.
# A composed line writes that with `hold' against `step' -- longer is a
# slide, shorter is a retrigger -- and a keyboard writes it by letting
# go or not.
#
# `poly = 2' is one voice sounding and one finishing: without it a
# retrigger would cut the previous note's release off where it stood.
#
# The filter is filt::svf, in hertz, with its cutoff an expression: where
# `cutoff' rests plus however much of `depth' the filter envelope is
# asking for.
#
# THE ACCENT is the 303's, and it is the reason a bass line written for
# this file sounds like a performance rather than a sequence. On a real
# one, the accent switch does not simply turn a step up: it puts a pulse
# into the filter's envelope and, through a capacitor that has not
# finished discharging, into the resonance as well. So an accented note
# is brighter and squelchier than the one before it, and the notes
# around it are not touched. Here `Accent Threshold' is where a velocity
# starts counting as one, and everything above it is scaled across the
# rest of the range -- so a line whose steps sit at 70 and 110 accents
# the 110s and leaves the 70s alone, which is exactly what `gen::accent'
# and a `bassline' already write.
#
# With `Accent Depth' and `Accent Resonance' at zero this is the graph
# it was before, note for note.

name "Bass";
author "Misha Nasledov";
description "A monophonic bass: overlapping notes slide, separated ones retrigger.";

    @glide = 60 ms;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 1000ms;
    @glide.label = "Glide";

    @sub = 0.4;
    @sub.widget = 1;
    @sub.min = 0;
    @sub.max = 1;
    @sub.label = "Sub Octave";

    @cutoff = 220;
    @cutoff.widget = 1;
    @cutoff.min = 40;
    @cutoff.max = 4000;
    @cutoff.label = "Cutoff (Hz)";

    @depth = 1800;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 8000;
    @depth.label = "Envelope Depth (Hz)";

    @track = 0;
    @track.widget = 1;
    @track.min = 0;
    @track.max = 2;
    @track.label = "Key Follow";

    @res = 0.8;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Resonance";

    @accent = 0.72;
    @accent.widget = 1;
    @accent.min = 0.1;
    @accent.max = 0.95;
    @accent.label = "Accent Threshold";

    @accdepth = 2200;
    @accdepth.widget = 1;
    @accdepth.min = 0;
    @accdepth.max = 8000;
    @accdepth.label = "Accent Depth (Hz)";

    @accres = 0.15;
    @accres.widget = 1;
    @accres.min = 0;
    @accres.max = 0.5;
    @accres.label = "Accent Resonance";

    @drive = 2.2;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

    @fa = 2 ms;
    @fa.widget = 1;
    @fa.min = 0;
    @fa.max = 500ms;
    @fa.label = "Filter Attack";
    @fd = 180 ms;
    @fd.widget = 1;
    @fd.min = 0;
    @fd.max = 3000ms;
    @fd.label = "Filter Decay";
    @fs = 0.1;
    @fs.widget = 1;
    @fs.min = 0;
    @fs.max = 1;
    @fs.label = "Filter Sustain";
    @fr = 120 ms;
    @fr.widget = 1;
    @fr.min = 0;
    @fr.max = 2000ms;
    @fr.label = "Filter Release";

    @a = 2 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 500ms;
    @a.label = "Attack";
    @d = 400 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 3000ms;
    @d.label = "Decay";
    @s = 0.55;
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @r = 90 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 2000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    mono = 1;
    poly = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The slide. Its state is the voice's, so it carries across a retune;
# with nothing lagging the frequency, mono is a hard retune, which is
# also a sound.
node glide misc::slew {
    in = freq->out;
    time = @glide;
};

node osc osc::simple {
    freq = glide->out;
    waveform = 1;
};

node sub osc::simple {
    freq = glide->out * 0.5;
    waveform = 2;
};

node fenv env::adsr {
    a = @fa;
    d = @fd;
    s = @fs;
    r = @fr;
    trigger = ionode->trigger;
};

# Nothing below the threshold, and the rest of the velocity range
# stretched across the whole of the accent. The division is safe at
# every setting because `Accent Threshold' stops at 0.95.
node accent math::clamp {
    in = (ionode->velocity - @accent) / (1 - @accent);
    lo = 0;
    hi = 1;
};

# Where the sub gives up.
#
# It plays an octave below the note, so in the bottom octave of the
# keyboard it plays *below hearing*: at MIDI 24 the note is 32.7 Hz and
# the sub is 16.4 Hz, and 16 Hz is not a pitch -- it is sixteen pulses a
# second, and what it does to the sound is put a flutter on it rather
# than weight underneath it. Measured as the depth of the amplitude
# modulation left in the 5 to 40 Hz band, a held note came out at 101%
# at MIDI 24 and at 0% from MIDI 43 up: the instrument turns grainy over
# an octave and a half and there is nothing in the panel that says so.
#
# So the sub fades out below 120 Hz and is gone by 55 Hz -- roughly the
# bottom octave of a bass guitar, which is where its own octave stops
# being audible as one. Above 120 Hz nothing here changes at all, which
# is most of what anything plays. The same clamp scales what is taken
# off the oscillators above, so the fade does not also make the low
# notes quieter: as the sub leaves, the rest comes up to meet it.
#
# It does not make MIDI 24 clean, and cannot. With the sub gone the
# note's own fundamental is still 32.7 Hz and still inside that band --
# 82% of the modulation is left, and all of it is the note. What this
# buys is the octave between: MIDI 28 to 40 goes from 97-100% to under
# 17%. Below that, the answer is to play higher.
node subamt math::clamp {
    in = (glide->out - 55) / 65;
    lo = 0;
    hi = 1;
};

# `Key Follow', and it is zero, so this graph is the one it was until
# somebody turns it up. A 303's filter does not track the keyboard and
# neither did this -- which is why a low note passes eleven harmonics of
# the saw where a high one barely passes its fundamental, and why the
# bottom of the keyboard is the bright end of this instrument. That is
# period-correct and worth keeping as the default; it is also the first
# thing to reach for when a composed line wants one timbre across two
# octaves, which a gen:: stage asks for constantly and a player never
# does.
#
# Octaves of cutoff per octave of pitch, not hertz per hertz: a filter
# follows a keyboard the way a keyboard is laid out, which is
# exponential, and the additive spelling of this barely moves at all --
# adding 262 Hz to a cutoff already in the kilohertz is a few per cent.
# So it is a ratio against the pitch the panel was tuned at, C2 at
# 65.4 Hz, which is the middle of what a bass plays: at C2 the cutoff is
# exactly what `Cutoff' says whatever `Key Follow' is, and elsewhere it
# moves `Key Follow' octaves for every octave of pitch. At 1 the timbre
# holds: the spectral centroid measured 1.55 times the note across MIDI
# 48 to 84, against 1.42 falling to 0.51 with this at zero. The range
# goes to 2 because tracking harder than the pitch is a sound as well.
#
# The envelope is inside the multiply rather than beside it, so a
# tracked filter sweeps from and to the pitch it is tracking. That is
# what summing the two in the exponential domain does on the instrument
# this is imitating, and the alternative -- an absolute sweep on top of
# a relative rest -- is neither.
#
# How far the envelope opens the filter is scaled by velocity, which is
# what makes an accented note of a bass line brighter and not only
# louder -- and `Accent Depth' is a second helping of that, on top, for
# the steps that cross the threshold. The resonance goes up with it,
# clamped where filt::svf's own range ends, because that is the half of
# the 303's accent that makes the squelch.
node filt filt::svf {
    in = osc->out * 0.5 * (1 - @sub * subamt->out) +
         sub->out * 0.5 * @sub * subamt->out;
    cutoff = (@cutoff + fenv->out * (@depth + accent->out * @accdepth) *
              ionode->velocity) * pow(glide->out / 65.4064, @track);
    res = clamp(@res + accent->out * @accres, 0, 0.99);
};

node drive dist::saturate {
    in = filt->out_low;
    factor = @drive;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = ionode->velocity * @s;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = drive->out;
    in1 = env->out;
};

io ionode;
