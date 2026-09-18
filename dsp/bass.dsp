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

    @res = 0.8;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.99;
    @res.label = "Resonance";

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

# How far the envelope opens the filter is scaled by velocity, which is
# what makes an accented note of a bass line brighter and not only
# louder.
node filt filt::svf {
    in = osc->out * 0.5 * (1 - @sub) + sub->out * 0.5 * @sub;
    cutoff = @cutoff + fenv->out * @depth * ionode->velocity;
    res = @res;
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
