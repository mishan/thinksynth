# Talk box -- a synth said through a mouth.
#
# The talk box was a speaker driver pushing a synth or a guitar up a
# plastic tube into the player's mouth, and a microphone in front of it:
# the instrument made the pitch and the timbre, and the mouth shaped the
# words. Roger Troutman's funk, then "Around the World" and "Digital
# Love". Here osc::speak is the mouth: two detuned saws through a
# low-pass are wired into its `source' with `talk' at 1, so the words a
# composed note carries (`ionode->say', from xform::say) are said by the
# synth. A keyboard note says nothing and opens on AA.
#
# The saws glide between notes over `Glide', which with a mono channel is
# the slurred, sliding line the box is known for; `Tone' is how bright
# the synth is before the mouth gets it.

name "Talk Box";
author "Misha Nasledov";
description "A synth lead said through a mouth: the talk box of funk and French house, saying the words its notes carry.";
category "Synths";

    @glide = 60 ms;
    @glide.widget = 1;
    @glide.min = 0 ms;
    @glide.max = 400 ms;
    @glide.label = "Glide";

    @detune = 8;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 30;
    @detune.label = "Detune (cents)";

    @tone = 3500;
    @tone.widget = 1;
    @tone.min = 500;
    @tone.max = 12000;
    @tone.label = "Tone (Hz)";

    @pace = 1;
    @pace.widget = 1;
    @pace.min = 0.5;
    @pace.max = 2;
    @pace.label = "Pace";

    @formants = 1;
    @formants.widget = 1;
    @formants.min = 0.6;
    @formants.max = 1.4;
    @formants.label = "Formants";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 1;
    out0 = vca->out;
    play = env->play;
};

node note misc::midi2freq {
    note = ionode->note;
};

node freq misc::slew {
    in = note->out;
    time = @glide;
};

node saw1 osc::blep {
    freq = freq->out * exp2(@detune / 1200);
    waveform = 0;
};

node saw2 osc::blep {
    freq = freq->out * exp2(-@detune / 1200);
    waveform = 0;
};

node tone filt::svf {
    in = (saw1->out + saw2->out) * 0.5;
    cutoff = @tone;
    res = 0.1;
};

node sp osc::speak {
    freq = freq->out;
    say = ionode->say;
    trigger = ionode->trigger;
    rate = @pace;
    shift = @formants;
    source = tone->out_low;
    talk = 1;
};

# Up while the mouth is saying something, so the words decide when the
# note ends.
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
