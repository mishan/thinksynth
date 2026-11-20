# Theremin -- a voice for two hands in the air, through a camera.
#
# A sine, as a theremin's tone nearly is, with a breath of its parabola for
# body, a slow vibrato, and a glide: the hands profile (hid/thhHandTrack.h)
# bends the note by a tracker's frames, a few dozen a second, and the glide
# is what makes that a slide rather than steps. The note's aux3 hushes it:
# the hands profile sends how far the left hand is below the top of the
# picture, so a raised hand is loud and a lowered one silent, and a key,
# which sends none, plays out. `Glide' is how long a slide takes,
# `Body' the parabola, `Vibrato' its depth in cents, `Level' the whole. See
# hid/thhHandTrack.h.

name "Theremin";
author "Misha Nasledov";
description "A sine with a glide, hushed by the note's aux3: for hands in the air.";
category "Experiments";

    @glide = 60 ms;
    @glide.widget = 1;
    @glide.min = 0ms;
    @glide.max = 500ms;
    @glide.label = "Glide";

    @body = 0.2;
    @body.widget = 1;
    @body.min = 0;
    @body.max = 1;
    @body.label = "Body";

    @vibrato = 12;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 50;
    @vibrato.label = "Vibrato";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = vca->out;
    out1 = vca->out;
    play = env->play;
};

node slide misc::slew {
    in = ionode->note;
    time = @glide;
};

node freq misc::midi2freq {
    note = slide->out;
};

node vib misc::vibrato {
    in = freq->out;
    rate = 5;
    depth = @vibrato;
};

node sine osc::simple {
    freq = vib->out;
    waveform = 0;
};

node body osc::simple {
    freq = vib->out;
    waveform = 5;
};

node env env::adsr {
    a = 15 ms;
    d = 0;
    s = 1;
    r = 120 ms;
    trigger = ionode->trigger;
};

# The hand moves at the tracker's rate, so its hush is glided too.
node hush misc::slew {
    in = ionode->aux3;
    time = 30 ms;
};

node vca mixer::mul {
    in0 = sine->out * (1 - @body) + body->out * @body;
    in1 = env->out * (1 - hush->out) * @level * 0.5;
};

io ionode;
