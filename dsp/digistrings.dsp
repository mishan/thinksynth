# Digital Strings -- the JD-800's strings, bright and clean.
#
# Where the string machine is a crowd of saws made into a section by a
# chorus, the early-nineties digital strings were a recording -- a PCM
# loop of a real section -- with the bow's attack as a sample of its own
# on top, and a synth's filter and envelope over both. Clean where the
# Solina wobbles, and bright to the point of glassy: Orbital's long pads,
# the strings over half of the rave records from 1991 on.
#
# There is no recording here, so the section is three saws a few cents
# apart and a fourth an octave up, which is the brightness a PCM string
# loop carries above its fundamental. THE BOW is a burst of band-passed
# noise for its first moment, which is what the JD's attack waves were.
# THE SHEEN is a high-pass of the section laid back over the low-pass,
# `Air' of it -- the top end a digital synth kept and an analogue one
# rolled off. The filter follows the key, so a high note is as bright as
# a low one rather than duller, and the vibrato comes in late, as a
# player's does.
#
# Put fx/ensemble.dsp or fx/chorus.dsp on the channel for the JD's own
# chorus; the voice is dry so a chord moves as one.

name "Digital Strings";
author "Misha Nasledov";
description "Bright PCM-era strings in the manner of the JD-800: a clean section, a bowed attack and a glassy top.";
category "Strings and pads";

    @a = 180 ms;
    @a.widget = 1;
    @a.min = 5 ms;
    @a.max = 3000 ms;
    @a.label = "Attack";
    @a.group = "Envelope";

    @r = 700 ms;
    @r.widget = 1;
    @r.min = 20 ms;
    @r.max = 5000 ms;
    @r.label = "Release";
    @r.group = "Envelope";

    @detune = 7;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 25;
    @detune.label = "Detune (cents)";

    @cutoff = 5;
    @cutoff.widget = 1;
    @cutoff.min = 1;
    @cutoff.max = 20;
    @cutoff.label = "Brightness (x pitch)";

    @air = 0.35;
    @air.widget = 1;
    @air.min = 0;
    @air.max = 1;
    @air.label = "Air";

    @bow = 0.25;
    @bow.widget = 1;
    @bow.min = 0;
    @bow.max = 1;
    @bow.label = "Bow";

    @vibrato = 12;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 40;
    @vibrato.label = "Vibrato (cents)";

    @level = 0.7;
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

node note misc::midi2freq {
    note = ionode->note;
};

node freq misc::vibrato {
    in = note->out;
    rate = 5.5;
    depth = @vibrato;
    delay = 400 ms;
    rise = 600 ms;
};

node saw1 osc::blep { freq = freq->out;                           waveform = 0; };
node saw2 osc::blep { freq = freq->out * exp2(@detune / 1200);    waveform = 0; };
node saw3 osc::blep { freq = freq->out * exp2(-@detune / 1200);   waveform = 0; };
node saw4 osc::blep { freq = freq->out * 2 * exp2(@detune / 2400); waveform = 0; };

node section mixer::add {
    in0 = (saw1->out + saw2->out + saw3->out) * 0.3;
    in1 = saw4->out * 0.15;
};

node tone filt::svf {
    in = section->out;
    cutoff = min(note->out * @cutoff, 16000);
    res = 0.15;
};

node sheen filt::svf {
    in = section->out;
    cutoff = 6000;
    res = 0;
};

node noise osc::noise { };

node rosin filt::svf {
    in = noise->out;
    cutoff = 3200;
    res = 0.5;
};

node scrape env::ad {
    a = 2 ms;
    d = 90 ms;
    p = 1;
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
    in0 = (tone->out_low + sheen->out_high * @air +
           rosin->out_band * scrape->out * @bow) * @level;
    in1 = env->out;
};

io ionode;
