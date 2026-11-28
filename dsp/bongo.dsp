# West Coast bongo -- a struck tone, folded and gated.
#
# Two osc::fmop operators, the modulator at `Ratio' times the note and
# pushing the carrier's phase by `Index', through dist::fold, whose
# `Timbre' folds the wave back on itself for its brightness -- the West
# Coast's way, where the East's filters a bright wave down. Then
# filt::lpg, struck by the note: a vactrol that opens the low pass and the
# gain together and lets them close at their own slow pace, so the note
# goes dark as it goes quiet. That is the plucked, woody "bongo" the low
# pass gate is known for. Harder notes strike it harder and fold further.
#
# The gate's times are milliseconds as plain numbers: filt::lpg reads them
# in ms itself.

name "West Coast bongo";
author "Misha Nasledov";
description "An FM pair, wavefolded and struck through a low-pass gate: a woody, plucked tone.";
category "Plucked";

    @ratio = 1.41;
    @ratio.widget = 1;
    @ratio.min = 0.5;
    @ratio.max = 4;
    @ratio.label = "Ratio";

    @index = 1.5;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 6;
    @index.label = "Index";

    @timbre = 2.5;
    @timbre.widget = 1;
    @timbre.min = 0.5;
    @timbre.max = 10;
    @timbre.label = "Timbre";

    @symmetry = 0.1;
    @symmetry.widget = 1;
    @symmetry.min = -0.5;
    @symmetry.max = 0.5;
    @symmetry.label = "Symmetry";

    @decay = 220;
    @decay.widget = 1;
    @decay.min = 30;
    @decay.max = 1500;
    @decay.label = "Decay (ms)";

node ionode {
    channels = 2;
    out0 = gate->out;
    out1 = gate->out;
    play = gate->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node modulator osc::fmop {
    freq = freq->out;
    ratio = @ratio;
    reset = ionode->trigger;
};

node carrier osc::fmop {
    freq = freq->out;
    mod = modulator->out;
    index = @index * (0.5 + 0.5 * ionode->velocity);
    reset = ionode->trigger;
};

node fold dist::fold {
    in = carrier->out * 0.8;
    drive = @timbre * (0.6 + 0.4 * ionode->velocity);
    symmetry = @symmetry;
};

node gate filt::lpg {
    in = fold->out;
    strike = ionode->trigger;
    level = 0.4 + 0.6 * ionode->velocity;
    open = 3;
    close = @decay;
    res = 0.2;
};

io ionode;
