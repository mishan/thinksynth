# Sing -- a legato voice that sings the words its notes carry.
#
# The mouth is dsp/speak.dsp's, osc::speak; the rest is a singer's.
#
# ONE VOICE, AND OVERLAP IS A SLIDE. `mono = 1' retunes the sounding
# voice when a note arrives over a held one, and osc::speak takes the new
# note's words without restarting its resonators, so a line whose `hold'
# is longer than its `step' is sung legato: the consonants between two
# vowels are said on the way from one to the next. The glide is a
# misc::slew on the note rather than on hertz, so a fifth takes as long as
# a second does, as a singer's portamento does.
#
# A THROAT IS NOT A CLOCK. `jitter' and `shimmer' let each glottal cycle
# stray a little in length and level, at a healthy voice's figures;
# vibrato is a singer's half semitone at 5.5 Hz, arriving once the
# phrase's first note has settled and carrying over its slides. `track'
# lifts F1 over the fundamental on the high notes, where a soprano opens
# her jaw. The velocity is `effort': a soft phrase is darker and a loud
# one brighter, not only quieter, and a slide keeps the velocity of the
# note it started from. `Ring' is the singer's formant around 3 kHz.
#
# `Detune' moves the pitch by cents, so that copies of this voice on
# separate channels singing the same line do not comb into one; give each
# a different `Formants' too and they are different singers.

name "Sing";
author "Misha Nasledov";
description "A legato sung voice: overlapping notes slide and carry their syllables, with a singer's vibrato, waver and brightness.";
category "Synths";

    @glide = 70 ms;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 500 ms;
    @glide.label = "Glide";

    @formants = 1.2;
    @formants.widget = 1;
    @formants.min = 0.8;
    @formants.max = 1.4;
    @formants.label = "Formants";

    @vibrato = 50;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 100;
    @vibrato.label = "Vibrato (cents)";

    @vibrate = 5.5;
    @vibrate.widget = 1;
    @vibrate.min = 3;
    @vibrate.max = 8;
    @vibrate.label = "Vibrato Rate (Hz)";

    @breath = 0.1;
    @breath.widget = 1;
    @breath.min = 0;
    @breath.max = 1;
    @breath.label = "Breath";

    @ring = 4;
    @ring.widget = 1;
    @ring.min = 0;
    @ring.max = 12;
    @ring.label = "Ring (dB)";

    @detune = 0;
    @detune.widget = 1;
    @detune.min = -50;
    @detune.max = 50;
    @detune.label = "Detune (cents)";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    mono = 1;
    out0 = vca->out;
    play = env->play;
};

node glide misc::slew {
    in = ionode->note + @detune / 100;
    time = @glide;
};

node freq misc::midi2freq {
    note = glide->out;
};

node vib misc::vibrato {
    in = freq->out;
    rate = @vibrate;
    depth = @vibrato;
    delay = 200 ms;
    rise = 300 ms;
};

node sp osc::speak {
    freq = vib->out;
    say = ionode->say;
    trigger = ionode->trigger;
    shift = @formants;
    breath = @breath;
    jitter = 1;
    shimmer = 0.6;
    track = 1;
    tilt = 12;
    effort = ionode->velocity / th_max;
    ring = @ring;
};

# Up while the singer is singing, so the words decide when the voice
# ends.
node env env::adsr {
    a = 5 ms;
    d = 1 ms;
    s = 1;
    r = 80 ms;
    trigger = sp->play;
};

node vca mixer::mul {
    in0 = sp->out * @level * (0.4 + 0.6 * ionode->velocity / th_max);
    in1 = env->out;
};

io ionode;
