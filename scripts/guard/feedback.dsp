# feedback.dsp -- a graph with a real cycle in it, kept for the code that has
# to cope with one.
#
# Beside the harness rather than in dsp/, for the reason the other fixtures
# here give: it is not an instrument and every corpus sweep globs dsp/.
#
# THE CYCLE: `osc' is frequency-modulated by `delay', and `delay' is fed from
# `osc'. The engine breaks a loop like that by letting one node read the
# previous window, so the loop's delay *is* the window length -- 23 ms at
# 1024 frames, 5.3 ms at 256. The file therefore sounds different at
# different buffer sizes, which is why it is a fixture and not a preset, and
# why DSP_FORMAT.md tells authors not to write one.
#
# It is the last cycle in the tree. Two things were checked against it and one
# other file and have nothing else to run on:
#
#   scripts/dspab -B 256      that a buffer-size change is audible here and
#                             nowhere else (docs/JAM.md)
#   the node editor's layout  back-edges: a feedback arc set, reversed for
#                             layering (docs/NODE_EDITOR.md)
#
# This is dsp/noargs/dfb.dsp, 2004, by Leif Ames. Its `dcalc' node writes `in'
# to a `misc::freq2samples' whose input is called `freq', so nothing reads it
# and nothing ever did; left as it was, because what is being preserved is the
# graph somebody actually wrote.

name "Guard: feedback";
description "An oscillator FM'd by a delay line fed from itself. Window-length dependent by construction.";

node ionode {
    channels = 2;
    out0 = mixer->out;
    out1 = mixer->out;
    play = env->play;

    dlen = 44100;
    fmamt = 0.1;
    fmul = 1;
    pmul = 2;
    dfreq = freqmul->out;
    dwave = 3;
    dfeed = 0.3;
    ddry = 0;

    waveform = 1;
    cutoff = 0.24;
    res = 0.7;

    a = 2800;
    d = 4000;
    s = 0.7;    # 1 = full, 0 = off
    r = 5000;
};

node suscalc math::mul {
    in0 = ionode->velocity;
    in1 = ionode->s;
};

node env env::adsr {
    a = ionode->a;
    d = ionode->d;
    s = suscalc->out;
    r = ionode->r;
    p = ionode->velocity;
    trigger = ionode->trigger;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node freqmul math::mul {
    in0 = freq->out;
    in1 = ionode->fmul;
};

node osc osc::simple {
    freq = freq->out;
    waveform = ionode->waveform;
    fm = delay->out;
    fmamt = ionode->fmamt;
};

node dosc osc::simple {
    freq = freqmul->out;
    waveform = ionode->dwave;
};

node dcalc misc::freq2samples {
    in = freq->out;
};

node dmul math::mul {
    in0 = freq->out;
    in1 = ionode->pmul;
};

node dmap env::map {
    in = dosc->out;
    inmin = th_min;
    inmax = th_max;
    outmin = 1;
    outmax = dmul->out;
};

node filt filt::ink2 {
    in = osc->out;
    cutoff = ionode->cutoff;
    res = ionode->res;
};

node delay delay::echo {
    in = osc->out;
    size = ionode->dlen;
    delay = dmap->out;
    feedback = ionode->dfeed;
    dry = ionode->ddry;
};

node mixer mixer::mul {
    in0 = filt->out;
    in1 = env->out;
};

io ionode;