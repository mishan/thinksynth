# Analog -- a polysynth voice that is never quite in tune.
#
# What makes a vintage polysynth sound like one is mostly what its
# designers were fighting: oscillators that wander a few cents, a
# filter that sits slightly differently on every voice, two copies of a
# saw that are never in phase. Take that away and the same circuit
# sounds like a plug-in. This graph puts it back on purpose.
#
# THE OSCILLATORS DO NOT ALIAS. Three osc::blep saws (or pulses, or
# triangles) at the center and `Unison' cents either side, a second
# oscillator `Interval' semitones away, and a square an octave down for
# the sub. PolyBLEP is what keeps a high note from growing tones that
# are not in it.
#
# EVERY OSCILLATOR DRIFTS ON ITS OWN. A misc::drift per oscillator moves
# its pitch up to `Drift' cents at about a third of a hertz, seeded from
# the note and the oscillator so no two wander alike -- and a piece
# replays the same, because the seed is the note and not a clock. The
# cutoff drifts too, by a few percent.
#
# THE UNISON IS SPREAD. The flat copy leans left and the sharp one right
# by `Spread', and the center one is in both: the beating between them
# becomes width rather than a wobble in the middle.
#
# THE FILTER IS 24 dB AN OCTAVE: two filt::svf low-passes in series,
# the resonance on the second, the cutoff opened by its own envelope and
# by velocity, and tracking the keyboard by `Key Track'.
#
# `Sync' restarts the second oscillator on every cycle of the center one,
# so its pitch becomes a formant over the center's, and `Sync Sweep'
# moves it by the filter envelope: the sync lead.
#
# `Bend' is the pitch wheel, in semitones and slewed over 40 ms: a piece
# rides it with a chanarg sink (`chanarg = "bend"').

name "Analog";
author "Misha Nasledov";
description "A drifting, band-limited polysynth: three unison oscillators spread in stereo, a second oscillator, a sub, and a 24 dB filter.";
category "Leads and stabs";

    @wave1 = 0;
    @wave1.widget = 1;
    @wave1.min = 0;
    @wave1.max = 2;
    @wave1.step = 1;
    @wave1.values = "Sawtooth,Pulse,Triangle";
    @wave1.label = "Wave";
    @wave1.group = "Oscillators";

    @wave2 = 1;
    @wave2.widget = 1;
    @wave2.min = 0;
    @wave2.max = 2;
    @wave2.step = 1;
    @wave2.values = "Sawtooth,Pulse,Triangle";
    @wave2.label = "Wave 2";
    @wave2.group = "Oscillators";

    @semi2 = -12;
    @semi2.widget = 1;
    @semi2.min = -24;
    @semi2.max = 24;
    @semi2.step = 1;
    @semi2.label = "Interval (semitones)";
    @semi2.group = "Oscillators";

    @mix2 = 0.3;
    @mix2.widget = 1;
    @mix2.min = 0;
    @mix2.max = 1;
    @mix2.label = "Osc 2 Level";
    @mix2.group = "Oscillators";

    @pw = 0.4;
    @pw.widget = 1;
    @pw.min = 0.05;
    @pw.max = 0.95;
    @pw.label = "Pulse Width";
    @pw.group = "Oscillators";

    @sync = 0;
    @sync.widget = 1;
    @sync.min = 0;
    @sync.max = 1;
    @sync.step = 1;
    @sync.values = "Off,On";
    @sync.label = "Sync";
    @sync.group = "Oscillators";

    @sweep = 0;
    @sweep.widget = 1;
    @sweep.min = 0;
    @sweep.max = 36;
    @sweep.label = "Sync Sweep (semitones)";
    @sweep.group = "Oscillators";

    @sub = 0.2;
    @sub.widget = 1;
    @sub.min = 0;
    @sub.max = 1;
    @sub.label = "Sub";
    @sub.group = "Oscillators";

    @unison = 7;
    @unison.widget = 1;
    @unison.min = 0;
    @unison.max = 40;
    @unison.label = "Unison (cents)";
    @unison.group = "Warmth";

    @spread = 0.7;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 1;
    @spread.label = "Spread";
    @spread.group = "Warmth";

    @drift = 4;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 25;
    @drift.label = "Drift (cents)";
    @drift.group = "Warmth";

    @bend = 0;
    @bend.widget = 1;
    @bend.min = -12;
    @bend.max = 12;
    @bend.label = "Bend (semitones)";
    @bend.group = "Oscillators";

    @cutoff = 1200;
    @cutoff.widget = 1;
    @cutoff.min = 40;
    @cutoff.max = 12000;
    @cutoff.label = "Cutoff (Hz)";
    @cutoff.group = "Filter";

    @res = 0.3;
    @res.widget = 1;
    @res.min = 0;
    @res.max = 0.95;
    @res.label = "Resonance";
    @res.group = "Filter";

    @envamt = 2500;
    @envamt.widget = 1;
    @envamt.min = 0;
    @envamt.max = 10000;
    @envamt.label = "Env Amount (Hz)";
    @envamt.group = "Filter";

    @keytrack = 0.5;
    @keytrack.widget = 1;
    @keytrack.min = 0;
    @keytrack.max = 1;
    @keytrack.label = "Key Track";
    @keytrack.group = "Filter";

    @fa = 5 ms;
    @fa.widget = 1;
    @fa.min = 0 ms;
    @fa.max = 3000 ms;
    @fa.label = "Attack";
    @fa.group = "Filter Envelope";

    @fd = 500 ms;
    @fd.widget = 1;
    @fd.min = 0 ms;
    @fd.max = 5000 ms;
    @fd.label = "Decay";
    @fd.group = "Filter Envelope";

    @fs = 0.3;
    @fs.widget = 1;
    @fs.min = 0.01;
    @fs.max = 1;
    @fs.label = "Sustain";
    @fs.group = "Filter Envelope";

    @fr = 400 ms;
    @fr.widget = 1;
    @fr.min = 0 ms;
    @fr.max = 5000 ms;
    @fr.label = "Release";
    @fr.group = "Filter Envelope";

    @a = 5 ms;
    @a.widget = 1;
    @a.min = 0 ms;
    @a.max = 3000 ms;
    @a.label = "Attack";
    @a.group = "Envelope";

    @d = 400 ms;
    @d.widget = 1;
    @d.min = 0 ms;
    @d.max = 5000 ms;
    @d.label = "Decay";
    @d.group = "Envelope";

    @s = 0.7;
    @s.widget = 1;
    @s.min = 0.01;
    @s.max = 1;
    @s.label = "Sustain";
    @s.group = "Envelope";

    @r = 300 ms;
    @r.widget = 1;
    @r.min = 70 ms;
    @r.max = 5000 ms;
    @r.label = "Release";
    @r.group = "Envelope";

node ionode {
    channels = 2;
    out0 = vcal->out;
    out1 = vcar->out;
    play = env->play;
};

node pitch misc::midi2freq {
    note = ionode->note;
};

node bend misc::slew {
    in = @bend;
    time = 40 ms;
};

node freq math::mul {
    in0 = pitch->out;
    in1 = exp2(bend->out / 12);
};

# One wanderer per oscillator, in cents, and one for the cutoff.
node driftc misc::drift { rate = 0.3;  depth = @drift; seed = ionode->note; };
node driftl misc::drift { rate = 0.37; depth = @drift; seed = ionode->note + 1000; };
node driftr misc::drift { rate = 0.29; depth = @drift; seed = ionode->note + 2000; };
node drift2 misc::drift { rate = 0.33; depth = @drift; seed = ionode->note + 3000; };
node driftf misc::drift { rate = 0.2;  depth = 0.05;   seed = ionode->note + 4000; };
node drifts misc::drift { rate = 0.31; depth = @drift; seed = ionode->note + 5000; };

node oscc osc::blep {
    freq = freq->out * exp2(driftc->out / 1200);
    waveform = @wave1;
    pw = @pw;
    phase = 0;
};
node oscl osc::blep {
    freq = freq->out * exp2((driftl->out - @unison) / 1200);
    waveform = @wave1;
    pw = @pw;
    phase = 0.33;
};
node oscr osc::blep {
    freq = freq->out * exp2((driftr->out + @unison) / 1200);
    waveform = @wave1;
    pw = @pw;
    phase = 0.67;
};
node osc2 osc::blep {
    freq = freq->out *
           exp2((@semi2 * 100 + drift2->out + fenv->out * @sweep * 100) /
                1200);
    waveform = @wave2;
    pw = @pw;
    phase = 0.5;
    reset = oscc->edge * @sync;
};
node subosc osc::blep {
    freq = freq->out * 0.5 * exp2(drifts->out / 1200);
    waveform = 1;
    pw = 0.5;
    phase = 0.25;
};

# What both sides share, and the unison leaning each way.
node common math::add {
    in0 = oscc->out + osc2->out * @mix2;
    in1 = subosc->out * @sub;
};
node mixl math::add {
    in0 = common->out + oscl->out * (1 + @spread) * 0.5;
    in1 = oscr->out * (1 - @spread) * 0.5;
};
node mixr math::add {
    in0 = common->out + oscr->out * (1 + @spread) * 0.5;
    in1 = oscl->out * (1 - @spread) * 0.5;
};

node fenv env::adsr {
    a = @fa;
    d = @fd;
    s = @fs;
    r = @fr;
    trigger = ionode->trigger;
};

node cut math::min {
    in0 = (@cutoff + fenv->out * @envamt * ionode->velocity) *
          exp2(@keytrack * (ionode->note - 60) / 12) * (1 + driftf->out);
    in1 = 18000;
};

node lp1l filt::svf { in = mixl->out * 0.2; cutoff = cut->out; res = 0; };
node lp2l filt::svf { in = lp1l->out_low; cutoff = cut->out; res = @res; };
node lp1r filt::svf { in = mixr->out * 0.2; cutoff = cut->out; res = 0; };
node lp2r filt::svf { in = lp1r->out_low; cutoff = cut->out; res = @res; };

node env env::adsr {
    a = @a;
    d = @d;
    s = @s * ionode->velocity;
    r = @r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node vcal mixer::mul { in0 = lp2l->out_low; in1 = env->out; };
node vcar mixer::mul { in0 = lp2r->out_low; in1 = env->out; };

io ionode;
