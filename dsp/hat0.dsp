# Hat 0 -- two squares ring-modulated, filtered by their own envelope.
#
# Velocity fades the release between `Closed Length' and `Open Length',
# which is the hi-hat's one trick: the same part plays both, and which
# one it is is in the hit.
#
# THE PEDAL. `choke = 1' makes that split a playable pair -- the open hat
# on the `and' is cut by the closed one that lands on the beat, because
# on a kit they are one instrument and a foot cannot be in two places.
# Which voice to end is the channel's knowledge and not a graph's, so the
# engine does the ending and `Pedal Close' only says how fast.

name "Hat 0";
author "Leif Ames";
description "Electronic Hihat";

    @freq = 940;
    @freq.widget = 1;
    @freq.min = 0;
    @freq.max = 3000;
    @freq.label = "Frequency";
    @freqmul = 3.141;
    @freqmul.widget = 1;
    @freqmul.min = 0;
    @freqmul.max = 8;
    @freqmul.label = "Frequency Multiply";
    @depth1 = 0.1;
    @depth1.widget = 1;
    @depth1.min = 0;
    @depth1.max = 1;
    @depth1.label = "Depth 1";
    @depth2 = 0.25;
    @depth2.widget = 1;
    @depth2.min = 0;
    @depth2.max = 1;
    @depth2.label = "Depth 2";

    @attack = 10 ms;
    @attack.widget = 1;
    @attack.min = 0;
    @attack.max = 500ms;
    @attack.label = "Attack";

    @olen = 1000 ms;
    @olen.widget = 1;
    @olen.min = 0;
    @olen.max = 4000ms;
    @olen.label = "Open Length";

    @clen = 200 ms;
    @clen.widget = 1;
    @clen.min = 0;
    @clen.max = 4000ms;
    @clen.label = "Closed Length";

    @midp = 0.2;
    @midp.widget = 1;
    @midp.min = 0;
    @midp.max = 1;
    @midp.label = "Envelope Midpoint";

    @cutmin = 0.4;
    @cutmin.widget = 1;
    @cutmin.min = 0;
    @cutmin.max = 1;
    @cutmin.label = "Filter Low";
    @cutmax = 0.7;
    @cutmax.widget = 1;
    @cutmax.min = 0;
    @cutmax.max = 1;
    @cutmax.label = "Filter High";

    @pedal = 12 ms;
    @pedal.widget = 1;
    @pedal.min = 1ms;
    @pedal.max = 4000ms;
    @pedal.label = "Pedal Close";

node ionode {
    channels = 2;

    # One hat sounding and one being cut: `choke' is what makes the pair
    # and `poly' is the room their overlap needs.
    choke = 1;
    poly = 2;

    out0 = mixer->out;
    out1 = mixer->out;
    play = adsr->play * foot->out;

    waveform = 2;
};

node vcurve math::mul {        # velocity curve
    in0 = ionode->velocity;
    in1 = ionode->velocity;
};

node decay mixer::fade {
    in0 = @clen;
    in1 = @olen;
    fade = vcurve->out;
};

node adsr env::adsr {
    a = 0;
    d = @attack;
    s = @midp;
    r = decay->out;
    trigger = 0;
};

node static osc::static {
};

node freqmul math::mul {
    in0 = @freq;
    in1 = @freqmul;
};

node osc1 osc::simple {
    freq = @freq;
    waveform = ionode->waveform;
    fm = static->out;
    fmamt = @depth1;
};

node osc2 osc::simple {
    freq = freqmul->out;
    waveform = ionode->waveform;
    fm = static->out;
    fmamt = @depth2;
};

node ringmod mixer::mul {
    in0 = osc1->out;
    in1 = osc2->out;
};

node filtmap env::map {
    in = adsr->out;
    inmin = 0;
    inmax = th_max;
    outmin = @cutmin;
    outmax = @cutmax;
};

node filter filt::ds {
    in = ringmod->out;
    cutoff = filtmap->out;
};

# The pedal. `choke = 1' on the io node sends every voice that is still
# sounding into its release when the next hat lands, and marks it by
# taking `trigger' negative -- so this is 1 for a key down, 1 for a key
# up, and 0 only for a voice the choke took. A note-off therefore does
# not shorten a hat, which is what a hat wants: its length is in the
# velocity. `play' is multiplied by it too, so a choked voice retires as
# soon as it is quiet rather than sitting out the rest of its decay.
#
# A `Pedal Close' at the top of its range is a hat that ignores the
# pedal: the release outlasts the hat's own envelope, so nothing is ever
# cut, which is the sound this graph had before it could be.
node foot env::adsr {
    a = 0;
    d = 0;
    s = th_max;
    r = @pedal;
    trigger = clamp(1 + ionode->trigger, 0, 1);
};

node mixer mixer::mul {
    in0 = filter->out_high;
    in1 = adsr->out * foot->out;
};

io ionode;