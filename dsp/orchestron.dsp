# Orchestron -- Kraftwerk's choir, from an optical disc.
#
# The Vako Orchestron played its voices off spinning discs of film, the
# way a film's soundtrack is read: a loop of a choir or a string section
# per key, short, band-limited and a little unsteady, since a disc never
# turns quite evenly. On "Trans-Europe Express" and "Radioactivity" it is
# the choir that sounds like a choir heard down a telephone line.
#
# Here the loop is two saws and a pulse, dark and narrow, through
# filt::vowel's five formants for the choir -- `Voice' at 0 is that `ah',
# and toward 1 the formants give way to a plain low-pass for the
# strings. THE DISC is a slow drift on every voice's pitch, each its own,
# and a slower wobble shared by the whole keyboard; THE READ is the
# low-pass that keeps it from ever being bright, and a little hiss.

name "Orchestron";
author "Misha Nasledov";
description "An optical-disc choir and strings in the manner of the Vako Orchestron: narrow, dark and a little unsteady.";
category "Strings and pads";

    @voice = 0;
    @voice.widget = 1;
    @voice.min = 0;
    @voice.max = 1;
    @voice.label = "Choir / Strings";

    @vowel = 0;
    @vowel.widget = 1;
    @vowel.min = 0;
    @vowel.max = 4;
    @vowel.label = "Vowel (a e i o u)";

    @wobble = 12;
    @wobble.widget = 1;
    @wobble.min = 0;
    @wobble.max = 40;
    @wobble.label = "Disc wobble (cents)";

    @dark = 2800;
    @dark.widget = 1;
    @dark.min = 800;
    @dark.max = 8000;
    @dark.label = "Read (Hz)";

    @hiss = 0.03;
    @hiss.widget = 1;
    @hiss.min = 0;
    @hiss.max = 0.2;
    @hiss.label = "Hiss";

    @a = 120 ms;
    @a.widget = 1;
    @a.min = 5 ms;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 400 ms;
    @r.widget = 1;
    @r.min = 20 ms;
    @r.max = 4000 ms;
    @r.label = "Release";

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

# The disc: each voice's own drift, and the motor's slower wobble.
node drift misc::drift { rate = 0.7; depth = @wobble; seed = ionode->note; };
node motor osc::simple { freq = 0.45; waveform = 0; };

node freq math::mul {
    in0 = note->out;
    in1 = exp2((drift->out + motor->out * @wobble * 0.5) / 1200);
};

node saw1 osc::blep { freq = freq->out * exp2(5 / 1200.0);  waveform = 0; };
node saw2 osc::blep { freq = freq->out * exp2(-5 / 1200.0); waveform = 0; };
node pulse osc::blep { freq = freq->out * 0.5; waveform = 1; pw = 0.3; };

node loop mixer::add {
    in0 = (saw1->out + saw2->out) * 0.4;
    in1 = pulse->out * 0.25;
};

node choir filt::vowel {
    in = loop->out;
    vowel = @vowel;
    gender = -0.3;
};

node noise osc::noise { };

node read filt::svf {
    in = choir->out * (1 - @voice) * 4.5 + loop->out * @voice +
         noise->out * @hiss;
    cutoff = @dark;
    res = 0.1;
};

node env env::adsr {
    a = @a;
    d = 10 ms;
    s = ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = read->out_low * @level;
    in1 = env->out;
};

io ionode;
