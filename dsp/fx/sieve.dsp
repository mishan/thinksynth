# Sieve -- the channel thinned to its loudest frequencies, or, inverted,
# emptied of them.
#
# fft::gate on each side, a frame late. Each frequency passes while it
# is within Thresh of the loudest one at that moment: at -30 dB a sound
# keeps its partials and loses the noise and hiss between them, and
# nearer 0 only its strongest partials are left, a thin whistling ghost
# of it. Inverted, it keeps what the gate would take: the breath, the
# bow noise and the room, with the notes taken out. Relative to the
# loudest, so it thins a quiet passage as it does a loud one.
#
# Smooth is how fast a frequency opens and shuts. Too short and the
# frequencies near Thresh chirp as they flicker; longer, the sieve
# moves with the sound rather than against it. Mix blends against the
# channel delayed by the same frame, so the two do not comb.
#
# Size, rounded to a power of two, is the frame and the latency: 4096
# separates close partials, 256 follows fast notes.

name "Sieve";
author "Misha Nasledov";
description "Spectral gate: keep a sound's loudest frequencies, or invert to keep everything under them.";
category "Effects";

    @thresh = -30;
    @thresh.widget = 1;
    @thresh.min = -90;
    @thresh.max = -1;
    @thresh.label = "Thresh (dB)";

    @invert = 0;
    @invert.widget = 1;
    @invert.min = 0;
    @invert.max = 1;
    @invert.values = "Off,On";
    @invert.label = "Invert";

    @smooth = 30 ms;
    @smooth.widget = 1;
    @smooth.min = 0 ms;
    @smooth.max = 2000 ms;
    @smooth.label = "Smooth";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

    @size = 2048;
    @size.widget = 1;
    @size.min = 256;
    @size.max = 4096;
    @size.label = "Size (samples)";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = svl->out;
    out1 = svr->out;
};

node svl fft::gate {
    in = ionode->in0;
    size = @size;
    thresh = @thresh;
    invert = @invert;
    smooth = @smooth;
    mix = @mix;
};

node svr fft::gate {
    in = ionode->in1;
    size = @size;
    thresh = @thresh;
    invert = @invert;
    smooth = @smooth;
    mix = @mix;
};

io ionode;
