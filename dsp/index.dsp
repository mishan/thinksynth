# Index -- a voice for a Valve Index controller to play.
#
# A sawtooth through a filter, with a breath of noise under it, made for a
# hand to keep shaping while the note sounds rather than set once: every knob
# here is one a controller's profile can ride. `Bright' opens the filter,
# `Wobble' is vibrato, `Growl' drives it harder into the filter's resonance,
# `Air' is breath, `Sub' adds a square an octave down, `Bend' moves the pitch
# two semitones either way, and `Level' and `Pan' place it. Each note's own
# pressure (aux0, from an MPE controller's key or a controller's `aux 0')
# opens its filter and lifts its level on top of the knobs, so one note of a
# chord can swell alone. See docs/HID.md.

name "Index";
author "Misha Nasledov";
description "A voice for a Valve Index controller: every knob a hand rides.";
category "Experiments";

    @bright = 0.5;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 1;
    @bright.label = "Bright";

    @wobble = 0;
    @wobble.widget = 1;
    @wobble.min = 0;
    @wobble.max = 1;
    @wobble.label = "Wobble";

    @growl = 0;
    @growl.widget = 1;
    @growl.min = 0;
    @growl.max = 1;
    @growl.label = "Growl";

    @air = 0;
    @air.widget = 1;
    @air.min = 0;
    @air.max = 1;
    @air.label = "Air";

    @sub = 0;
    @sub.widget = 1;
    @sub.min = 0;
    @sub.max = 1;
    @sub.label = "Sub";

    @bend = 0;
    @bend.widget = 1;
    @bend.min = -1;
    @bend.max = 1;
    @bend.label = "Bend";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

    @pan = 0;
    @pan.widget = 1;
    @pan.min = -1;
    @pan.max = 1;
    @pan.label = "Pan";

    @r = 300 ms;
    @r.widget = 1;
    @r.min = 5ms;
    @r.max = 4000ms;
    @r.label = "Release";

node ionode {
    channels = 2;
    out0 = place->out0;
    out1 = place->out1;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node env env::adsr {
    a = 8 ms;
    d = 300 ms;
    s = 0.8;
    r = @r;
    trigger = ionode->trigger;
};

# Vibrato at a singer's rate, half a semitone deep at most.
node vibrato osc::simple {
    freq = 5.5;
    waveform = 0;
};

node saw osc::blep {
    freq = freq->out * exp2((@bend * 2 + vibrato->out * @wobble * 0.5) / 12);
    waveform = 0;
};

node sub osc::blep {
    freq = freq->out * exp2((@bend * 2 + vibrato->out * @wobble * 0.5) / 12) / 2;
    waveform = 1;
};

node breath osc::noise {
    color = 0;
};

node drive dist::saturate {
    in = (saw->out + sub->out * @sub * 0.7 + breath->out * @air * 0.6) * 0.6;
    factor = 1 + @growl * 7;
};

# 200 Hz to 12 kHz, on an octave scale; full pressure adds half of
# Bright's travel, up to its top.
node tone filt::svf {
    in = drive->out;
    cutoff = 200 * pow(60, clamp(@bright + ionode->aux0 * 0.5, 0, 1));
    res = 0.2 + @growl * 0.5;
};

node vca mixer::mul {
    in0 = tone->out_low;
    in1 = env->out * ionode->velocity * @level * (1 + ionode->aux0 * 0.6);
};

node place mixer::pan {
    in = vca->out;
    pan = @pan;
};

io ionode;
