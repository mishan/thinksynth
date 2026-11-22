# Morph -- this channel's sound wearing another channel's shape: a pad
# that speaks with a voice's vowels, or pumps and hisses as a drum loop.
#
# fft::morph on each side, a frame late. This channel is the carrier,
# what is heard: its pitch, its notes, its timing. The channel the piece
# names as `side' is the modulator, whose levels it takes on, band by
# band:
#
#     instrument pad {
#         dsp    "pad.dsp";
#         effect "fx/morph.dsp" { side = drums; };
#     };
#
# `Morph' goes from the pad as it is to the pad at the modulator's levels,
# in decibels, so halfway sounds halfway. `Width' is the band the levels
# are taken over. Wide, a few hundred hertz and over the pad's pitch, it
# is a vocoder at the transform's resolution: the pad keeps its notes
# and takes the modulator's vowels and rhythm. Narrow, toward 0, the pad
# gives up its own levels for the modulator's, and what is left of it is
# its phase: the modulator, smeared and robotic.
#
# THE MODULATOR IS HEARD TOO. A side is the other channel's sound as it
# is mixed, so to hear only the morph, turn that channel down and `Side
# Level' up by the same amount. It scales the modulator as the morph
# takes it, so the output, which follows the modulator's level, is back
# where it was at Morph 1, and the carrier at Morph 0 is untouched --
# and it rides the same frames Morph does, so a change of both at once
# is never a frame of one without the other. It is linear: 4 is twelve
# decibels, 16 twenty-four.
#
# With no side named, an effect hears its own channel as its side, and a
# sound morphed by itself is itself: at Side Level 1 this passes the
# channel through, a frame late.
#
# Size, rounded to a power of two, is the frame and the latency: 2048
# resolves a low pad's partials, 512 keeps a drum's attacks closer.

name "Morph";
author "Misha Nasledov";
description "Spectral morph: this channel's sound taking on another channel's levels, band by band, from a vocoder to a smear.";
category "Effects";

    @morph = 1;
    @morph.widget = 1;
    @morph.min = 0;
    @morph.max = 1;
    @morph.label = "Morph";

    @width = 400;
    @width.widget = 1;
    @width.min = 0;
    @width.max = 4000;
    @width.label = "Width (Hz)";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

    @sidelevel = 1;
    @sidelevel.widget = 1;
    @sidelevel.min = 0;
    @sidelevel.max = 16;
    @sidelevel.label = "Side Level";

    @size = 2048;
    @size.widget = 1;
    @size.min = 256;
    @size.max = 4096;
    @size.label = "Size (samples)";

node ionode {
    channels = 2;

    # This channel's voices: the carrier.
    in0 = 0;
    in1 = 0;

    # The channel the piece named: the modulator.
    side0 = 0;
    side1 = 0;

    out0 = mol->out;
    out1 = mor->out;
};

node mol fft::morph {
    in = ionode->in0;
    side = ionode->side0 * @sidelevel;
    size = @size;
    morph = @morph;
    width = @width;
    mix = @mix;
};

node mor fft::morph {
    in = ionode->in1;
    side = ionode->side1 * @sidelevel;
    size = @size;
    morph = @morph;
    width = @width;
    mix = @mix;
};

io ionode;
