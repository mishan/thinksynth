# Bowl -- a singing bowl, struck once and left to ring.
#
# A bowl's partials are not a harmonic series but the modes of a bent
# metal shell, and they sit at roughly 1, 2.76, 5.40, 8.93, 13.3 and 18.6
# times the fundamental: that is what makes it a bowl and not a bell or a
# string. Each mode here is a sine at its ratio with its own decay, the
# higher ones dying sooner -- twenty seconds for the fundamental at
# `Ring = 1', two and a half for the sixth -- which is a struck bowl
# modeled mode by mode rather than by a filter. No filter in the tree
# holds a Q high enough to ring for twenty seconds, which is also why
# the drums here reach for filt::svf and a click rather than
# filt::resonator (see rim808.dsp).
#
# A real bowl's fundamental is two modes a fraction of a hertz apart,
# because the shell is never quite round, and the beating between them
# is the slow wah everybody hears in one. `Beat' is that gap in hertz;
# the fundamental's twin sits there above it.
#
# Each decay is env::ad's half cosine cubed, which starts as the hit and
# ends in a long tail rather than the half cosine's even fall -- closer
# to the exponential a real mode decays by. The note ends when the
# fundamental has.

name "Bowl";
author "Misha Nasledov";
description "A struck singing bowl: six inharmonic modes, the fundamental beating.";
category "Keys";

    @ring = 1;
    @ring.widget = 1;
    @ring.min = 0.1;
    @ring.max = 3;
    @ring.label = "Ring";

    @beat = 0.7;
    @beat.widget = 1;
    @beat.min = 0;
    @beat.max = 4;
    @beat.label = "Beat (Hz)";

    @bright = 0.5;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 1;
    @bright.label = "Brightness";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = e0->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# Decays in samples at Ring = 1: 20, 14, 9, 6, 4 and 2.5 seconds.
node e0 env::ad { a = 2 ms; d = 882000 * @ring; p = ionode->velocity; };
node e1 env::ad { a = 2 ms; d = 617400 * @ring; p = ionode->velocity; };
node e2 env::ad { a = 1 ms; d = 396900 * @ring; p = ionode->velocity; };
node e3 env::ad { a = 1 ms; d = 264600 * @ring; p = ionode->velocity; };
node e4 env::ad { a = 1 ms; d = 176400 * @ring; p = ionode->velocity; };
node e5 env::ad { a = 1 ms; d = 110250 * @ring; p = ionode->velocity; };

node m0  osc::simple { freq = freq->out;                waveform = 0; };
node m0b osc::simple { freq = freq->out + @beat;        waveform = 0; };
node m1  osc::simple { freq = freq->out * 2.76;         waveform = 0; };
node m2  osc::simple { freq = freq->out * 5.40;         waveform = 0; };
node m3  osc::simple { freq = freq->out * 8.93;         waveform = 0; };
node m4  osc::simple { freq = freq->out * 13.34;        waveform = 0; };
node m5  osc::simple { freq = freq->out * 18.64;        waveform = 0; };

# The upper modes' levels scale with `Brightness', which is how hard and
# with what the bowl was struck: a felt mallet at 0, a wooden one at 1.
node mix mixer::add {
    in0 = (m0->out + m0b->out) * 0.6 * e0->out * e0->out * e0->out +
          m1->out * 0.7 * e1->out * e1->out * e1->out;
    in1 = (m2->out * 0.5 * e2->out * e2->out * e2->out +
           m3->out * 0.32 * e3->out * e3->out * e3->out +
           m4->out * 0.2 * e4->out * e4->out * e4->out +
           m5->out * 0.12 * e5->out * e5->out * e5->out) * @bright * 2;
};

io ionode;
