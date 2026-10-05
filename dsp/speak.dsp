# Speak -- a voice that says the words its notes carry.
#
# osc::speak is a formant synthesizer: a glottal buzz and noise through
# resonators that move from one phoneme's shape to the next. A note from
# a composed line brings its syllable with it (`ionode->say'), so the
# melody is the chain's and the words are the piece's; a note from a
# keyboard says nothing and is sung on AA.
#
# THE VOWEL IS HELD FOR THE NOTE. The consonants before a syllable's
# vowel are spoken at their own pace, the vowel lasts as long as the key,
# and the consonants after it come at the release -- which is how a
# singer places words on a line. The envelope therefore follows the
# speaker's own `play' and not the key, or the final consonant of every
# word would be cut off with the note.
#
# `Robot' is the source: 0 a glottal pulse, the throat; 1 a raw
# sawtooth, the Votrax. `Formants' moves every resonance together, which
# is the size of the head: 1 a man, 1.15 a woman, under 0.8 a machine
# talking to itself.

name "Speak";
author "Misha Nasledov";
description "A formant voice that sings the syllables a composed line carries, held on the vowel; a keyboard note sings AA.";
category "Synths";

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

    @robot = 0.3;
    @robot.widget = 1;
    @robot.min = 0;
    @robot.max = 1;
    @robot.label = "Robot";

    @breath = 0.05;
    @breath.widget = 1;
    @breath.min = 0;
    @breath.max = 1;
    @breath.label = "Breath";

    @vibrato = 15;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 60;
    @vibrato.label = "Vibrato (cents)";

    @vibrate = 5.5;
    @vibrate.widget = 1;
    @vibrate.min = 2;
    @vibrate.max = 8;
    @vibrate.label = "Vibrato Rate (Hz)";

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

node freq misc::midi2freq {
    note = ionode->note;
};

node vib misc::vibrato {
    in = freq->out;
    rate = @vibrate;
    depth = @vibrato;
    delay = 250 ms;
    rise = 200 ms;
};

node sp osc::speak {
    freq = vib->out;
    say = ionode->say;
    trigger = ionode->trigger;
    rate = @pace;
    shift = @formants;
    buzz = @robot;
    breath = @breath;
};

# Up while the speaker is speaking, so the words decide when the voice
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
