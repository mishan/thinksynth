# Phaser -- six allpasses moving together, against the dry signal.
#
# An allpass filter changes nothing you can hear: every frequency comes
# out at the level it went in, only later, and by a different amount of
# later for each one. Add that to the untouched signal and the delays
# turn into cancellations -- wherever a frequency comes out half a cycle
# behind itself, it disappears. Six stages put three notches in the
# spectrum; sweep all six together with one LFO and the notches walk up
# and down, which is the whole effect.
#
# NOT A FLANGER, and the difference is worth writing down because the
# two are confused constantly. A flanger is a *delay*: its notches are
# at every odd multiple of one frequency, harmonically spaced, hundreds
# of them, and the sound is metallic. A phaser's notches come from a
# filter's phase response, and there are three of them at intervals that
# are not harmonic -- so a phaser sounds like something turning rather
# than something ringing. fx/flanger.dsp is the other one.
#
# ONE LFO FOR ALL SIX STAGES, by expression: each `freq' is the same
# `Centre * exp2(lfo * Sweep)'. Exponential rather than linear, so the
# notches move by an interval instead of by a number of hertz and the
# sweep sounds the same at the bottom of the range as at the top.
#
# NO FEEDBACK, and that is a deliberate omission rather than an
# oversight. A phaser's resonance comes from routing the last stage back
# into the first, and this graph language allows the cycle -- the file
# loads -- but the engine has no ordering that satisfies it, so the last
# stage's output arrives a whole *window* late. That makes the feedback
# path a delay of 256 samples in the browser and 1024 on the desktop,
# and the same file renders differently in the two. Six stages against
# the dry signal is a phaser; six stages with a window-length-dependent
# resonance is a bug with a knob on it.

name "Phaser";
author "Misha Nasledov";
description "Six allpass stages swept by one LFO against the dry signal.";
category "Effects";

    @centre = 600;
    @centre.widget = 1;
    @centre.min = 100;
    @centre.max = 4000;
    @centre.label = "Centre (Hz)";

    # In octaves either way, so the notches travel the same interval
    # wherever `Centre' is.
    @sweep = 1.4;
    @sweep.widget = 1;
    @sweep.min = 0;
    @sweep.max = 3;
    @sweep.label = "Sweep (octaves)";

    @rate = 0.35;
    @rate.widget = 1;
    @rate.min = 0.02;
    @rate.max = 8;
    @rate.label = "Rate (Hz)";

    # Half and half is where the notches are deepest: a cancellation
    # needs the two signals to be the same size. Away from it the
    # notches fill in, which is a gentler effect and not a different one.
    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# Triangle: the notches should spend their time travelling rather than
# sitting at the ends of the sweep, which is what a sine would do.
node lfo osc::simple {
    freq = @rate;
    waveform = 3;
};

# The two sweeps, computed once and read by six stages each. The right
# side divides where the left multiplies, so its notches travel down
# while the left's travel up -- which is the whole of the stereo here,
# and cheaper than a second LFO, which could not run backwards anyway:
# `mul = -1' on osc::simple gives a negative frequency, and thBoundFreq
# floors that to the slowest wave there is rather than reversing it.
#
# Clamped, because `Centre' at the top of its slider with three octaves
# of sweep asks for 32 kHz, and an allpass whose corner is past Nyquist
# is a wire.
node sweepl math::clamp {
    in = @centre * exp2(lfo->out * @sweep);
    lo = 40;
    hi = 16000;
};

node sweepr math::clamp {
    in = @centre / exp2(lfo->out * @sweep);
    lo = 40;
    hi = 16000;
};

node l0 filt::allpass { in = ionode->in0; freq = sweepl->out; };
node l1 filt::allpass { in = l0->out;     freq = sweepl->out; };
node l2 filt::allpass { in = l1->out;     freq = sweepl->out; };
node l3 filt::allpass { in = l2->out;     freq = sweepl->out; };
node l4 filt::allpass { in = l3->out;     freq = sweepl->out; };
node l5 filt::allpass { in = l4->out;     freq = sweepl->out; };

node r0 filt::allpass { in = ionode->in1; freq = sweepr->out; };
node r1 filt::allpass { in = r0->out;     freq = sweepr->out; };
node r2 filt::allpass { in = r1->out;     freq = sweepr->out; };
node r3 filt::allpass { in = r2->out;     freq = sweepr->out; };
node r4 filt::allpass { in = r3->out;     freq = sweepr->out; };
node r5 filt::allpass { in = r4->out;     freq = sweepr->out; };

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = l5->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = r5->out;
    fade = @mix;
};

io ionode;
