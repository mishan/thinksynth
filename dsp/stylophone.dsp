# Stylophone -- the pocket calculator's lead.
#
# Dubreq's stylophone was a metal keyboard printed on a circuit board and
# a stylus on a wire: touch a key and a single oscillator sounds, lift
# the stylus and it stops, no envelope at all. The oscillator is a
# buzzy, narrow pulse, and the little speaker it came out of had one
# resonance it put on everything. "Pocket Calculator", and "Space
# Oddity" before it.
#
# A narrow pulse at the note, a band-pass at the speaker's resonance
# laid over it, and the vibrato switch, which was a fixed wobble, on or
# off. The gate is a few milliseconds each way, which is the stylus
# touching and lifting -- with a click on the touch, as the real one
# had. Play it on a mono channel: it had one oscillator.

name "Stylophone";
author "Misha Nasledov";
description "The stylus-on-metal pocket synth: one buzzy pulse, its speaker's resonance and a vibrato switch.";
category "Leads and stabs";

    @vibrato = 0;
    @vibrato.widget = 1;
    @vibrato.min = 0;
    @vibrato.max = 1;
    @vibrato.step = 1;
    @vibrato.values = "Off,On";
    @vibrato.label = "Vibrato";

    @width = 0.25;
    @width.widget = 1;
    @width.min = 0.05;
    @width.max = 0.5;
    @width.label = "Pulse width";

    @speaker = 1400;
    @speaker.widget = 1;
    @speaker.min = 400;
    @speaker.max = 4000;
    @speaker.label = "Speaker (Hz)";

    @level = 0.4;
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

node wobble osc::simple { freq = 6.5; waveform = 0; };

node buzz osc::blep {
    freq = note->out * exp2(wobble->out * @vibrato * 40 / 1200);
    waveform = 1;
    pw = @width;
};

node speaker filt::svf {
    in = buzz->out;
    cutoff = @speaker;
    res = 0.6;
};

# The stylus landing on the key.
node click env::ad {
    a = 0;
    d = 3 ms;
    p = 1;
};

node env env::adsr {
    a = 3 ms;
    d = 1 ms;
    s = 1;
    r = 6 ms;
    p = 1;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = (buzz->out * 0.35 + speaker->out_band * 0.8 + click->out * 0.3) *
          @level;
    in1 = env->out;
};

io ionode;
