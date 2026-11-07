# MFM 01 -- two oscillators, one modulating the other, under two filters.
#
# The one file in the corpus that had no description and called itself
# `test'. It is not a test: three patches and a shipped piece play it.
# What it is, is a two-oscillator synth where the first oscillator is not
# heard -- it is the `fm' input of `osc2', so Waveform 1 chooses what
# modulates and Waveform 2 what is modulated, and FM Amount is how much.
# Both waveforms are selectable, which is what makes it more than a DX
# voice: a saw modulating a saw is not a pair of sines.
#
# The two filters are in series and swept by an LFO each -- a Moog ladder
# into an ink2 -- and what `Filter 1 LFO' and `Filter 2 LFO' set is the
# rate of each sweep rather than a cutoff. Both LFOs map into the same
# pair of bounds, so `Filter Low' and `Filter High' are the range the
# whole thing moves in and `Resonance' belongs to both filters.

name "MFM 01";
author "Misha Nasledov";
description "Two selectable waveforms, one modulating the other, under two LFO-swept filters.";
category "Synths";

    @wave1 = 5;
    @wave1.widget = 1;
    @wave1.min = 0;
    @wave1.max = 5;
    @wave1.label = "Waveform 1";
    @wave2 = 1;
    @wave2.widget = 1;
    @wave2.min = 0;
    @wave2.max = 5;
    @wave2.label = "Waveform 2";
    @fmamt = 0.8;
    @fmamt.widget = 1;
    @fmamt.min = 0;
    @fmamt.max = 5;
    @fmamt.label = "FM Amount";
    @filt1 = 1.23;
    @filt1.widget = 1;
    @filt1.min = 0;
    @filt1.max = 20;
    @filt1.label = "Filter 1 LFO";
    @filt2 = 1.21;
    @filt2.widget = 1;
    @filt2.min = 0;
    @filt2.max = 20;
    @filt2.label = "Filter 2 LFO";
    @filtlo = 0.3;
    @filtlo.widget = 1;
    @filtlo.min = 0;
    @filtlo.max = 1;
    @filtlo.label = "Filter Low";
    @filthi = 1;
    @filthi.widget = 1;
    @filthi.min = 0;
    @filthi.max = 1;
    @filthi.label = "Filter High";
    @filtres = 0.1;
    @filtres.widget = 1;
    @filtres.min = 0;
    @filtres.max = 1;
    @filtres.label = "Resonance";

    @a = 0.8 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000ms;
    @a.label = "Attack";
    @d = 20 ms;
    @d.widget = 1;
    @d.min = 0;
    @d.max = 2000ms;
    @d.label = "Decay";
    @s = 0.4;    # 1 = full, 0 = off
    @s.widget = 1;
    @s.min = 0;
    @s.max = 1;
    @s.label = "Sustain";
    @r = 200 ms;
    @r.widget = 1;
    @r.min = 0;
    @r.max = 5000ms;
    @r.label = "Release";

node ionode {
    channels=2;
    out0 = mixer->out;
    out1 = mixer->out;
    play = env->play;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = @s;
};

node env env::adsr {
    a = @a;
    d = @d;
    s = suscalc->out;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc1 osc::simple {
    freq = freq->out;
    waveform = @wave1;
};

node osc2 osc::simple {
    freq = freq->out;
    waveform = @wave2;
    fm = osc1->out;
    fmamt = @fmamt;
};

node lfo1 osc::simple {
    freq = @filt1;
    waveform = 0;
};

node lfo2 osc::simple {
    freq = @filt2;
    waveform = 0;
};

node lfomap1 env::map {
    in = lfo1->out;
    inmin = th_min;
    inmax = th_max;
    outmin = @filtlo;
    outmax = @filthi;
};

node lfomap2 env::map {
    in = lfo2->out;
    inmin = th_min;
    inmax = th_max;
    outmin = @filtlo;
    outmax = @filthi;
};

node filt1 filt::moog {
    in = osc2->out;
    cutoff = lfomap1->out;
    res = @filtres;
};

node filt2 filt::ink2 {
    in = filt1->out_low;
    cutoff = lfomap2->out;
    res = @filtres;
};

node mixer mixer::mul {
    in0 = filt2->out;
    in1 = env->out;
};

io ionode;