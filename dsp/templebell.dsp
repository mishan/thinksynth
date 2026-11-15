# Temple Bell -- a great bronze bell, swung at with a wooden beam.
#
# A Japanese temple bell is not a church bell made bigger. Its lip is
# straight, not flared, so its partials sit at their own ratios -- about
# 1, 1.40, 2.44, 2.57, 4.59, 7.68 and 13.1 times the lowest, read off a
# recording of the Hiroshima Peace Bell -- rather than the hum, prime,
# tierce and nominal that filt::bell tunes. The note is that lowest
# mode, the hum; the strike tone heard at the stroke is the pair near
# 2.5, an octave and a minor third above the note.
# Each mode is a sine with its own decay, as in bowl.dsp, and for the
# same reason: nothing in the tree with a Q holds for forty seconds.
#
# THE WAVERING. A cast bell is never quite round, so each of its low
# modes is two, a fraction of a hertz apart, and the slow swell and fade
# between them is what the bell is remembered for. The four lowest modes
# here have a twin `Beat' above them, each at its own multiple of it so
# that the four never swell together and the tail does not pulse.
#
# THE TAIL. At `Ring = 1' the hum lasts forty-five seconds and the
# highest mode two: what is left after ten seconds is the three lowest
# modes, beating, and after twenty the hum and its neighbor, which is
# how one sounds across a valley.
#
# THE BEAM. A bell this size is struck with a swinging log, and the
# first thing heard is wood on bronze: a dull thud of low noise, heavier
# and a little brighter the harder the swing. Velocity is the swing on
# the bell too -- its level, and how much of the upper modes it wakes.
# `Brightness' is the beam's face on top of that: worn soft at 0, the
# upper modes barely there, and hard and new at 1.
#
# Each decay is env::ad's half cosine cubed, as in bowl.dsp: a long
# tail rather than an even fall. The note ends when the hum has.

name "Temple Bell";
author "Misha Nasledov";
description "A great bronze temple bell: inharmonic modes in beating pairs, a long hum and a wooden beam's thud.";
category "Keys";

    @ring = 1;
    @ring.widget = 1;
    @ring.min = 0.1;
    @ring.max = 2;
    @ring.label = "Ring";

    @beat = 0.5;
    @beat.widget = 1;
    @beat.min = 0;
    @beat.max = 3;
    @beat.label = "Beat (Hz)";

    @bright = 0.5;
    @bright.widget = 1;
    @bright.min = 0;
    @bright.max = 1;
    @bright.label = "Brightness";

    @thud = 0.6;
    @thud.widget = 1;
    @thud.min = 0;
    @thud.max = 1;
    @thud.label = "Beam Thud";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = e0->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

# Decays in samples at Ring = 1: 45, 36, 24, 14, 8, 4 and 2 seconds at
# 44.1 kHz. A unit cannot scale a control inside an expression.
node e0 env::ad { a = 4 ms; d = 1984500 * @ring; };
node e1 env::ad { a = 4 ms; d = 1587600 * @ring; };
node e2 env::ad { a = 3 ms; d = 1058400 * @ring; };
node e3 env::ad { a = 3 ms; d = 617400 * @ring; };
node e4 env::ad { a = 2 ms; d = 352800 * @ring; };
node e5 env::ad { a = 2 ms; d = 176400 * @ring; };
node e6 env::ad { a = 2 ms; d = 88200 * @ring; };

node m0  osc::simple { freq = freq->out;                       waveform = 0; };
node m0b osc::simple { freq = freq->out + @beat;               waveform = 0; };
node m1  osc::simple { freq = freq->out * 1.40;                waveform = 0; };
node m1b osc::simple { freq = freq->out * 1.40 + @beat * 1.62; waveform = 0; };
node m2  osc::simple { freq = freq->out * 2.44;                waveform = 0; };
node m2b osc::simple { freq = freq->out * 2.44 + @beat * 0.73; waveform = 0; };
node m3  osc::simple { freq = freq->out * 2.57;                waveform = 0; };
node m3b osc::simple { freq = freq->out * 2.57 + @beat * 2.31; waveform = 0; };
node m4  osc::simple { freq = freq->out * 4.59;                waveform = 0; };
node m5  osc::simple { freq = freq->out * 7.68;                waveform = 0; };
node m6  osc::simple { freq = freq->out * 13.1;                waveform = 0; };

node noise osc::noise {
    color = 0;
    amp = 1;
};

node blow env::ad {
    a = 1 ms;
    d = 45 ms;
    p = ionode->velocity * ionode->velocity;
};

node beam filt::svf {
    in = noise->out * blow->out;
    cutoff = 200 + 600 * ionode->velocity;
    res = 0.3;
};

# Each twin is quieter than its mode, so a pair dips without
# canceling.
node mix mixer::add {
    in0 = ((m0->out + m0b->out * 0.8) * 0.12 * e0->out * e0->out * e0->out +
           (m1->out + m1b->out * 0.7) * 0.15 * e1->out * e1->out * e1->out +
           (m2->out + m2b->out * 0.8) * 0.25 * e2->out * e2->out * e2->out +
           (m3->out + m3b->out * 0.6) * 0.1 * e3->out * e3->out * e3->out) *
          ionode->velocity;
    in1 = (m4->out * 0.22 * e4->out * e4->out * e4->out +
           m5->out * 0.1 * e5->out * e5->out * e5->out +
           m6->out * 0.06 * e6->out * e6->out * e6->out) *
          @bright * 2 * ionode->velocity * ionode->velocity +
          beam->out_low * @thud * 4;
};

io ionode;
