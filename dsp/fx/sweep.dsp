# Sweep -- a resonant low-pass opened and closed over bars.
#
# The filter at the heart of a build: `Position' is where the cutoff is,
# from `Low' to `High' on an octave scale, and a piece rides it from a
# gen::steps row on `fx.position' -- shut for the breakdown, opening over
# sixteen bars into the drop. `Cycle' adds a slow sine of that many bars
# of the piece's tempo, `Depth' wide, for the sweep that never stops; 0
# bars is none. Resonance high is the squelch an acid line wants; low, it
# is only a wall the music comes through.

name "Sweep";
author "Misha Nasledov";
description "A resonant low-pass ridden over bars, by hand or by a slow sine at the piece's tempo.";
category "Effects";

    @position = 1;
    @position.widget = 1;
    @position.min = 0;
    @position.max = 1;
    @position.label = "Position";

    @low = 150;
    @low.widget = 1;
    @low.min = 40;
    @low.max = 2000;
    @low.label = "Low (Hz)";

    @high = 16000;
    @high.widget = 1;
    @high.min = 1000;
    @high.max = 18000;
    @high.label = "High (Hz)";

    @res = 0.5;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";

    @bars = 0;
    @bars.widget = 1;
    @bars.min = 0;
    @bars.max = 32;
    @bars.label = "Cycle (bars)";

    @depth = 0.3;
    @depth.widget = 1;
    @depth.min = 0;
    @depth.max = 1;
    @depth.label = "Depth";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = filtl->out_low;
    out1 = filtr->out_low;
};

node tempo misc::tempo { };

# A bar is four beats; 0 bars gives a rate of 0, a sine standing still at 0.
node lfo osc::simple {
    freq = tempo->bpm / 240 / max(@bars, 0.0001) * clamp(@bars * 1000000, 0, 1);
    waveform = 0;
};

node pos math::clamp {
    in = @position + lfo->out * @depth * clamp(@bars * 1000000, 0, 1);
    lo = 0;
    hi = 1;
};

node cut math::mul {
    in0 = @low;
    in1 = pow(@high / @low, pos->out);
};

node filtl filt::svf {
    in = ionode->in0;
    cutoff = cut->out;
    res = @res;
};

node filtr filt::svf {
    in = ionode->in1;
    cutoff = cut->out;
    res = @res;
};

io ionode;
