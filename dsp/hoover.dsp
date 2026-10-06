# Hoover -- the rave lead: detuned saws and a narrow pulse, diving up into
# the note.
#
# Three sawtooths a few cents apart and a pulse an octave down with a
# thin `Width', all band-limited (osc::blep), so the stack is fat without
# aliasing. Every note starts `Dive' semitones under its pitch and slides
# up to it over `Dive Time', which is the swoop the sound is named for,
# and a chorus on the way out spreads it across the speakers. A low-pass
# opened by the velocity keeps soft notes from shrieking.

name "Hoover";
author "Misha Nasledov";
description "A detuned saw-and-pulse rave lead that swoops up into every note.";
category "Leads and stabs";

    @dive = 7;
    @dive.widget = 1;
    @dive.min = 0;
    @dive.max = 24;
    @dive.label = "Dive (semitones)";

    @divetime = 180 ms;
    @divetime.widget = 1;
    @divetime.min = 1ms;
    @divetime.max = 2000ms;
    @divetime.label = "Dive Time";

    @detune = 18;
    @detune.widget = 1;
    @detune.min = 0;
    @detune.max = 60;
    @detune.label = "Detune (cents)";

    @width = 0.15;
    @width.widget = 1;
    @width.min = 0.02;
    @width.max = 0.5;
    @width.label = "Width";

    @cutoff = 4500;
    @cutoff.widget = 1;
    @cutoff.min = 300;
    @cutoff.max = 16000;
    @cutoff.label = "Cutoff (Hz)";

    @a = 5 ms;
    @a.widget = 1;
    @a.min = 1ms;
    @a.max = 2000ms;
    @a.label = "Attack";

    @r = 250 ms;
    @r.widget = 1;
    @r.min = 10ms;
    @r.max = 4000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = choirl->out;
    out1 = choirr->out;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node swoop env::ad {
    a = 0;
    d = @divetime;
};

node pitch math::mul {
    in0 = freq->out;
    in1 = exp2(swoop->out * @dive / -12);
};

node saw0 osc::blep { freq = pitch->out;                            waveform = 0; };
node saw1 osc::blep { freq = pitch->out * exp2(@detune / 1200);     waveform = 0; };
node saw2 osc::blep { freq = pitch->out / exp2(@detune / 1200);     waveform = 0; };
node pulse osc::blep { freq = pitch->out * 0.5; waveform = 1; pw = @width; };

node filt filt::svf {
    in = (saw0->out + saw1->out + saw2->out + pulse->out) * 0.25;
    cutoff = @cutoff * (0.4 + 0.6 * ionode->velocity);
    res = 0.2;
};

node env env::adsr {
    a = @a;
    d = 1 ms;
    s = th_max;
    r = @r;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = filt->out_low;
    in1 = env->out * ionode->velocity;
};

node choirl delay::chorus {
    in = vca->out;
    rate = 0.8;
    depth = 60;
    delay = 400;
    taps = 2;
    mix = 0.5;
    phase = 0;
};

node choirr delay::chorus {
    in = vca->out;
    rate = 0.8;
    depth = 60;
    delay = 400;
    taps = 2;
    mix = 0.5;
    phase = 0.5;
};

io ionode;
