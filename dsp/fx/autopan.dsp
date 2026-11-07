# Autopan -- a channel that moves from one speaker to the other in time.
#
# One cycle every `Beats' of the piece's tempo, through misc::tempo, so a
# delay timed in beats and a pan timed in beats move together. `Depth' is
# how far it goes: at 1 each side is silent at the far end of its swing,
# at 0.3 the image breathes rather than travels. Each side keeps its own
# signal and is turned down while the other has the sound, so a stereo
# source stays stereo.
#
# The cycle's rate follows the tempo; its phase is from when the effect
# was loaded, not from the bar.

name "Autopan";
author "Misha Nasledov";
description "A channel swung between the speakers once every so many beats of the piece's tempo.";
category "Effects";

    @beats = 4;
    @beats.widget = 1;
    @beats.min = 0.25;
    @beats.max = 32;
    @beats.label = "Cycle (beats)";

    @depth = 0.7;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 1;
    @depth.label = "Depth";

    @shape = 0;
    @shape.widget = 1;
    @shape.min = 0;
    @shape.max = 3;
    @shape.step = 1;
    @shape.values = "Sine,Saw,Square,Triangle";
    @shape.label = "Shape";

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = ionode->in0 * (1 - @depth * max(swing->out, 0));
    out1 = ionode->in1 * (1 - @depth * max(0 - swing->out, 0));
};

node tempo misc::tempo { };

node lfo osc::simple {
    freq = tempo->bpm / 60 / @beats;
    waveform = @shape;
};

# Rounded off over 3 ms, so a square's flip and a saw's wrap move the
# image rather than clicking it.
node swing misc::slew {
    in = lfo->out;
    time = 3 ms;
};

io ionode;
