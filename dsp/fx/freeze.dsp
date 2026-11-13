# Freeze -- the channel held: a moment of it goes on as a sound of its own.
#
# fft::freeze on each side. With Freeze off it is a wire, bit for bit;
# turned on, it takes what the channel was playing in the last Size
# samples and holds it, fading from the channel to the held sound over
# Fade, and back when it is turned off. Random at 0 carries the held
# sound on as itself, a chord sustained past its release; towards 1 its
# phases are new every frame, a shimmer. The sides have seeds of their
# own, so that shimmer is wide rather than one sound in the middle.
#
# Size is the trade: 4096 holds a chord's pitches cleanly and smears its
# attack, 256 keeps a click a click and blurs pitch into a band. A piece
# turns Freeze on and off from a piece knob bound to it, or from a chain
# through `sink { instrument = ...; chanarg = "fx.freeze"; }'.

name "Freeze";
author "Misha Nasledov";
description "Spectral freeze: hold a moment of the channel as a sustained sound.";
category "Effects";

    @freeze = 0;
    @freeze.widget = 1;
    @freeze.min = 0;
    @freeze.max = 1;
    @freeze.step = 1;
    @freeze.label = "Freeze";

    @size = 2048;
    @size.widget = 1;
    @size.min = 256;
    @size.max = 4096;
    @size.label = "Size (samples)";

    @random = 0.3;
    @random.widget = 1;
    @random.min = 0;
    @random.max = 1;
    @random.label = "Random";

    @fade = 60 ms;
    @fade.widget = 1;
    @fade.min = 0;
    @fade.max = 5000ms;
    @fade.label = "Fade";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = fzl->out;
    out1 = fzr->out;
};

node fzl fft::freeze {
    in = ionode->in0;
    freeze = @freeze;
    size = @size;
    random = @random;
    fade = @fade;
    seed = 0;
};

node fzr fft::freeze {
    in = ionode->in1;
    freeze = @freeze;
    size = @size;
    random = @random;
    fade = @fade;
    seed = 1;
};

io ionode;
