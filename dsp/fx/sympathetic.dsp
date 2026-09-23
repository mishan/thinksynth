# Sympathetic -- the rest of a piano's strings, ringing along.
#
# A played string moves the bridge and the bridge moves every other
# string. Those whose dampers are off ring in sympathy wherever a partial
# of what was played lands near one of theirs: with the sustain pedal
# down that is every string on the instrument, and the halo it makes is
# most of the difference between a piano played with the pedal and one
# played without it.
#
# A voice cannot hear the other voices, so this is an effect: it runs on
# the channel's sum. `filt::sympathetic' is eighty-eight lightly damped
# string loops, one per key, fed what the channel played. The io node
# declares `pedal', and the engine writes the channel's sustain pedal
# there every window, 0 up to 1 down -- the pedal a keyboard or a piece
# sends is the pedal this hears, half-pedaling included.
#
# With the pedal up only the top nineteen keys, F#6 up, ring: a grand has
# no dampers there, so even a dry passage has a little shimmer over it.
# With it down, everything does.
#
# `Amount' is how much of the bank goes back on top of the dry channel.
# `Sustain' is a free string's T60 at middle C, longer below and shorter
# above; `Damper' is a damped one's; `Tone' is how much darker each trip
# round a string is.
#
# Made for dsp/grand.dsp, and it will put a pedal on anything: a pad
# through it is a pad with a piano's strings behind it.

name "Sympathetic";
author "Misha Nasledov";
description "A piano's other strings, ringing in sympathy: the sustain pedal's halo.";
category "Effects";

    @amount = 0.3;
    @amount.widget = 1;
    @amount.min = 0;
    @amount.max = 1;
    @amount.label = "Amount";

    # Seconds, as a plain number: a unit in a .dsp is folded into samples.
    @sustain = 15;
    @sustain.widget = 1;
    @sustain.min = 0.5;
    @sustain.max = 30;
    @sustain.label = "Sustain (s)";

    @damper = 0.1;
    @damper.widget = 1;
    @damper.min = 0.02;
    @damper.max = 2;
    @damper.label = "Damper (s)";

    @tone = 0.3;
    @tone.widget = 1;
    @tone.min = 0;
    @tone.max = 0.9;
    @tone.label = "Tone";

node ionode {
    channels = 2;

    # The engine writes these every window: the channel, and its pedal.
    in0 = 0;
    in1 = 0;
    pedal = 0;

    out0 = ionode->in0 + strings->out * @amount;
    out1 = ionode->in1 + strings->out * @amount;
};

node strings filt::sympathetic {
    in = (ionode->in0 + ionode->in1) * 0.5;
    pedal = ionode->pedal;
    undamped = 90;
    decay = @sustain;
    damper = @damper;
    damp = @tone;
};

io ionode;
