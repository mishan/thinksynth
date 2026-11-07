# Grab -- the voice for what a pair of Index controllers loops.
#
# A soft FM bell, so the loops ring apart from the hands' own voice
# (dsp/index.dsp) on the channel the hands play. `Bright' is how much the
# bell rings over its plain tone, `Ring' how long, and `Level' and `Pan'
# place the loops; the left hand rides them in the Grab profile. Each
# note's pressure (aux0) brightens it alone. See docs/HID.md.

name "Grab";
author "Misha Nasledov";
description "The bell a pair of Index controllers' loops play on.";
category "Experiments";

    @bright = 0.5;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 1;
    @bright.label = "Bright";

    @ring = 1500 ms;
    @ring.widget = 1;
    @ring.min = 100ms;
    @ring.max = 8000ms;
    @ring.label = "Ring";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

    @pan = 0;
    @pan.widget = 1;
    @pan.min = -1;
    @pan.max = 1;
    @pan.label = "Pan";

node ionode {
    channels = 2;
    out0 = place->out0;
    out1 = place->out1;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# The ring fades faster than the tone, as a struck bell's does.
node idx env::ad {
    a = 1 ms;
    d = @ring * 0.5;
};

node modop osc::fmop {
    freq = freq->out;
    ratio = 3.5;
};

node carrier osc::fmop {
    freq = freq->out;
    ratio = 1;
    mod = modop->out;
    index = (0.1 + 0.9 * idx->out) * clamp(@bright + ionode->aux0 * 0.5, 0, 1) *
            6 * ionode->velocity;
};

node env env::adsr {
    a = 2 ms;
    d = @ring;
    s = 0.15;
    r = 600 ms;
    trigger = ionode->trigger;
};

node vca mixer::mul {
    in0 = carrier->out * 0.45;
    in1 = env->out * @level;
};

node place mixer::pan {
    in = vca->out;
    pan = @pan;
};

io ionode;
