# nonfinite.dsp -- a graph that produces a NaN on purpose, for the guard in
# thMidiChan::mixNote to catch. One note only: middle C.
#
# Beside the harness rather than in dsp/, because every corpus sweep globs dsp/
# and would fail on it.
#
# `offset' is the note minus middle C, and `nan' divides that by itself: 0/0 on
# middle C, and 1 on every other note. So a middle C is a voice that cannot
# help going non-finite and any other note is an ordinary one -- which is what
# lets the gate hold both on the *same channel* at once and watch the good one
# survive. A fixture that was bad on every note could only ever show that a
# whole channel had been isolated, which is a weaker claim than the guard
# makes.
#
# math::div is the offender because it is not a filter: 0/0 is undefined
# arithmetic and no stability clamp can take it away.
#
# See scripts/dspsweep.cpp and scripts/guard/nonfinite.gen.

name "Guard: non-finite";
description "Middle C is 0/0; every other note is an ordinary voice.";

node ionode {
    out0 = poison->out;
    out1 = poison->out;
    channels = 2;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node osc osc::simple {
    freq = freq->out;
    waveform = 0;
};

node env env::adsr {
    a = 200;
    d = 4000;
    s = 0.4;
    r = 4000;
    p = 0.8;
    trigger = ionode->trigger;
};

node mix mixer::mul {
    in0 = osc->out;
    in1 = env->out;
};

node offset math::sub {
    in0 = ionode->note;
    in1 = 60;
};

node nan math::div {
    in0 = offset->out;
    in1 = offset->out;
};

node poison mixer::mul {
    in0 = mix->out;
    in1 = nan->out;
};

io ionode;
