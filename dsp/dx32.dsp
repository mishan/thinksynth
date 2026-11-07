# DX 32 -- six carriers and no modulators, the DX7's algorithm 32.
#
# Additive synthesis: six sines summed, op 6's feedback the only FM left,
# which turns one of them toward a sawtooth. Set up as an organ -- the
# ratios are drawbar footages, 16' to 2' -- because six sines at
# harmonic ratios with square envelopes is exactly what a drawbar organ
# is.
#
# THE KNOBS ARE THE DX7'S, op by op: a frequency ratio, a detune in
# cents, an output level and a velocity sensitivity, and an env::dx with
# four rates and four levels, all 0 to 99. Output level is in the
# envelope's own decibels -- 0.75 dB a step -- so on a modulator it is
# the brightness and on a carrier the volume, as on the hardware.
# Velocity sensitivity, 0 to 7, takes up to 25 dB off an op played
# softly; on a modulator that is a duller note rather than a quieter one.
#
# THE WIRING IS THE ALGORITHM. Each op is an osc::fmop and a modulator's
# output, through its envelope and level, is summed onto the `mod' of the
# op it feeds; the sum is scaled so a modulator at 99 swings the phase by
# 8 radians. Another algorithm is another file, not a knob -- see
# osc::fmop's head for why. `Feedback', 0 to 7 as on the panel, is op 6
# on itself.

name "DX 32: Six Carriers";
author "Misha Nasledov";
description "Six FM operators all heard, the DX7's algorithm 32, set up as an organ.";
category "Keys";

    @o1ratio = 0.5;
    @o1ratio.widget = 1;
    @o1ratio.min = 0.5;
    @o1ratio.max = 31;
    @o1ratio.label = "Ratio";
    @o1ratio.group = "Op 1";

    @o1det = 0;
    @o1det.widget = 1;
    @o1det.min = -50;
    @o1det.max = 50;
    @o1det.label = "Detune (cents)";
    @o1det.group = "Op 1";

    @o1level = 92;
    @o1level.widget = 1;
    @o1level.min = 0;
    @o1level.max = 99;
    @o1level.step = 1;
    @o1level.label = "Level";
    @o1level.group = "Op 1";

    @o1vel = 0;
    @o1vel.widget = 1;
    @o1vel.min = 0;
    @o1vel.max = 7;
    @o1vel.step = 1;
    @o1vel.label = "Velocity";
    @o1vel.group = "Op 1";

    @o1r1 = 99;
    @o1r1.widget = 1;
    @o1r1.min = 0;
    @o1r1.max = 99;
    @o1r1.step = 1;
    @o1r1.label = "Rate 1";
    @o1r1.group = "Op 1";

    @o1r2 = 99;
    @o1r2.widget = 1;
    @o1r2.min = 0;
    @o1r2.max = 99;
    @o1r2.step = 1;
    @o1r2.label = "Rate 2";
    @o1r2.group = "Op 1";

    @o1r3 = 99;
    @o1r3.widget = 1;
    @o1r3.min = 0;
    @o1r3.max = 99;
    @o1r3.step = 1;
    @o1r3.label = "Rate 3";
    @o1r3.group = "Op 1";

    @o1r4 = 70;
    @o1r4.widget = 1;
    @o1r4.min = 0;
    @o1r4.max = 99;
    @o1r4.step = 1;
    @o1r4.label = "Rate 4";
    @o1r4.group = "Op 1";

    @o1l1 = 99;
    @o1l1.widget = 1;
    @o1l1.min = 0;
    @o1l1.max = 99;
    @o1l1.step = 1;
    @o1l1.label = "Level 1";
    @o1l1.group = "Op 1";

    @o1l2 = 99;
    @o1l2.widget = 1;
    @o1l2.min = 0;
    @o1l2.max = 99;
    @o1l2.step = 1;
    @o1l2.label = "Level 2";
    @o1l2.group = "Op 1";

    @o1l3 = 99;
    @o1l3.widget = 1;
    @o1l3.min = 0;
    @o1l3.max = 99;
    @o1l3.step = 1;
    @o1l3.label = "Level 3";
    @o1l3.group = "Op 1";

    @o1l4 = 0;
    @o1l4.widget = 1;
    @o1l4.min = 0;
    @o1l4.max = 99;
    @o1l4.step = 1;
    @o1l4.label = "Level 4";
    @o1l4.group = "Op 1";

    @o2ratio = 1;
    @o2ratio.widget = 1;
    @o2ratio.min = 0.5;
    @o2ratio.max = 31;
    @o2ratio.label = "Ratio";
    @o2ratio.group = "Op 2";

    @o2det = 0;
    @o2det.widget = 1;
    @o2det.min = -50;
    @o2det.max = 50;
    @o2det.label = "Detune (cents)";
    @o2det.group = "Op 2";

    @o2level = 99;
    @o2level.widget = 1;
    @o2level.min = 0;
    @o2level.max = 99;
    @o2level.step = 1;
    @o2level.label = "Level";
    @o2level.group = "Op 2";

    @o2vel = 0;
    @o2vel.widget = 1;
    @o2vel.min = 0;
    @o2vel.max = 7;
    @o2vel.step = 1;
    @o2vel.label = "Velocity";
    @o2vel.group = "Op 2";

    @o2r1 = 99;
    @o2r1.widget = 1;
    @o2r1.min = 0;
    @o2r1.max = 99;
    @o2r1.step = 1;
    @o2r1.label = "Rate 1";
    @o2r1.group = "Op 2";

    @o2r2 = 99;
    @o2r2.widget = 1;
    @o2r2.min = 0;
    @o2r2.max = 99;
    @o2r2.step = 1;
    @o2r2.label = "Rate 2";
    @o2r2.group = "Op 2";

    @o2r3 = 99;
    @o2r3.widget = 1;
    @o2r3.min = 0;
    @o2r3.max = 99;
    @o2r3.step = 1;
    @o2r3.label = "Rate 3";
    @o2r3.group = "Op 2";

    @o2r4 = 70;
    @o2r4.widget = 1;
    @o2r4.min = 0;
    @o2r4.max = 99;
    @o2r4.step = 1;
    @o2r4.label = "Rate 4";
    @o2r4.group = "Op 2";

    @o2l1 = 99;
    @o2l1.widget = 1;
    @o2l1.min = 0;
    @o2l1.max = 99;
    @o2l1.step = 1;
    @o2l1.label = "Level 1";
    @o2l1.group = "Op 2";

    @o2l2 = 99;
    @o2l2.widget = 1;
    @o2l2.min = 0;
    @o2l2.max = 99;
    @o2l2.step = 1;
    @o2l2.label = "Level 2";
    @o2l2.group = "Op 2";

    @o2l3 = 99;
    @o2l3.widget = 1;
    @o2l3.min = 0;
    @o2l3.max = 99;
    @o2l3.step = 1;
    @o2l3.label = "Level 3";
    @o2l3.group = "Op 2";

    @o2l4 = 0;
    @o2l4.widget = 1;
    @o2l4.min = 0;
    @o2l4.max = 99;
    @o2l4.step = 1;
    @o2l4.label = "Level 4";
    @o2l4.group = "Op 2";

    @o3ratio = 1.5;
    @o3ratio.widget = 1;
    @o3ratio.min = 0.5;
    @o3ratio.max = 31;
    @o3ratio.label = "Ratio";
    @o3ratio.group = "Op 3";

    @o3det = 0;
    @o3det.widget = 1;
    @o3det.min = -50;
    @o3det.max = 50;
    @o3det.label = "Detune (cents)";
    @o3det.group = "Op 3";

    @o3level = 88;
    @o3level.widget = 1;
    @o3level.min = 0;
    @o3level.max = 99;
    @o3level.step = 1;
    @o3level.label = "Level";
    @o3level.group = "Op 3";

    @o3vel = 0;
    @o3vel.widget = 1;
    @o3vel.min = 0;
    @o3vel.max = 7;
    @o3vel.step = 1;
    @o3vel.label = "Velocity";
    @o3vel.group = "Op 3";

    @o3r1 = 99;
    @o3r1.widget = 1;
    @o3r1.min = 0;
    @o3r1.max = 99;
    @o3r1.step = 1;
    @o3r1.label = "Rate 1";
    @o3r1.group = "Op 3";

    @o3r2 = 99;
    @o3r2.widget = 1;
    @o3r2.min = 0;
    @o3r2.max = 99;
    @o3r2.step = 1;
    @o3r2.label = "Rate 2";
    @o3r2.group = "Op 3";

    @o3r3 = 99;
    @o3r3.widget = 1;
    @o3r3.min = 0;
    @o3r3.max = 99;
    @o3r3.step = 1;
    @o3r3.label = "Rate 3";
    @o3r3.group = "Op 3";

    @o3r4 = 70;
    @o3r4.widget = 1;
    @o3r4.min = 0;
    @o3r4.max = 99;
    @o3r4.step = 1;
    @o3r4.label = "Rate 4";
    @o3r4.group = "Op 3";

    @o3l1 = 99;
    @o3l1.widget = 1;
    @o3l1.min = 0;
    @o3l1.max = 99;
    @o3l1.step = 1;
    @o3l1.label = "Level 1";
    @o3l1.group = "Op 3";

    @o3l2 = 99;
    @o3l2.widget = 1;
    @o3l2.min = 0;
    @o3l2.max = 99;
    @o3l2.step = 1;
    @o3l2.label = "Level 2";
    @o3l2.group = "Op 3";

    @o3l3 = 99;
    @o3l3.widget = 1;
    @o3l3.min = 0;
    @o3l3.max = 99;
    @o3l3.step = 1;
    @o3l3.label = "Level 3";
    @o3l3.group = "Op 3";

    @o3l4 = 0;
    @o3l4.widget = 1;
    @o3l4.min = 0;
    @o3l4.max = 99;
    @o3l4.step = 1;
    @o3l4.label = "Level 4";
    @o3l4.group = "Op 3";

    @o4ratio = 2;
    @o4ratio.widget = 1;
    @o4ratio.min = 0.5;
    @o4ratio.max = 31;
    @o4ratio.label = "Ratio";
    @o4ratio.group = "Op 4";

    @o4det = 0;
    @o4det.widget = 1;
    @o4det.min = -50;
    @o4det.max = 50;
    @o4det.label = "Detune (cents)";
    @o4det.group = "Op 4";

    @o4level = 92;
    @o4level.widget = 1;
    @o4level.min = 0;
    @o4level.max = 99;
    @o4level.step = 1;
    @o4level.label = "Level";
    @o4level.group = "Op 4";

    @o4vel = 0;
    @o4vel.widget = 1;
    @o4vel.min = 0;
    @o4vel.max = 7;
    @o4vel.step = 1;
    @o4vel.label = "Velocity";
    @o4vel.group = "Op 4";

    @o4r1 = 99;
    @o4r1.widget = 1;
    @o4r1.min = 0;
    @o4r1.max = 99;
    @o4r1.step = 1;
    @o4r1.label = "Rate 1";
    @o4r1.group = "Op 4";

    @o4r2 = 99;
    @o4r2.widget = 1;
    @o4r2.min = 0;
    @o4r2.max = 99;
    @o4r2.step = 1;
    @o4r2.label = "Rate 2";
    @o4r2.group = "Op 4";

    @o4r3 = 99;
    @o4r3.widget = 1;
    @o4r3.min = 0;
    @o4r3.max = 99;
    @o4r3.step = 1;
    @o4r3.label = "Rate 3";
    @o4r3.group = "Op 4";

    @o4r4 = 70;
    @o4r4.widget = 1;
    @o4r4.min = 0;
    @o4r4.max = 99;
    @o4r4.step = 1;
    @o4r4.label = "Rate 4";
    @o4r4.group = "Op 4";

    @o4l1 = 99;
    @o4l1.widget = 1;
    @o4l1.min = 0;
    @o4l1.max = 99;
    @o4l1.step = 1;
    @o4l1.label = "Level 1";
    @o4l1.group = "Op 4";

    @o4l2 = 99;
    @o4l2.widget = 1;
    @o4l2.min = 0;
    @o4l2.max = 99;
    @o4l2.step = 1;
    @o4l2.label = "Level 2";
    @o4l2.group = "Op 4";

    @o4l3 = 99;
    @o4l3.widget = 1;
    @o4l3.min = 0;
    @o4l3.max = 99;
    @o4l3.step = 1;
    @o4l3.label = "Level 3";
    @o4l3.group = "Op 4";

    @o4l4 = 0;
    @o4l4.widget = 1;
    @o4l4.min = 0;
    @o4l4.max = 99;
    @o4l4.step = 1;
    @o4l4.label = "Level 4";
    @o4l4.group = "Op 4";

    @o5ratio = 3;
    @o5ratio.widget = 1;
    @o5ratio.min = 0.5;
    @o5ratio.max = 31;
    @o5ratio.label = "Ratio";
    @o5ratio.group = "Op 5";

    @o5det = 0;
    @o5det.widget = 1;
    @o5det.min = -50;
    @o5det.max = 50;
    @o5det.label = "Detune (cents)";
    @o5det.group = "Op 5";

    @o5level = 82;
    @o5level.widget = 1;
    @o5level.min = 0;
    @o5level.max = 99;
    @o5level.step = 1;
    @o5level.label = "Level";
    @o5level.group = "Op 5";

    @o5vel = 0;
    @o5vel.widget = 1;
    @o5vel.min = 0;
    @o5vel.max = 7;
    @o5vel.step = 1;
    @o5vel.label = "Velocity";
    @o5vel.group = "Op 5";

    @o5r1 = 99;
    @o5r1.widget = 1;
    @o5r1.min = 0;
    @o5r1.max = 99;
    @o5r1.step = 1;
    @o5r1.label = "Rate 1";
    @o5r1.group = "Op 5";

    @o5r2 = 99;
    @o5r2.widget = 1;
    @o5r2.min = 0;
    @o5r2.max = 99;
    @o5r2.step = 1;
    @o5r2.label = "Rate 2";
    @o5r2.group = "Op 5";

    @o5r3 = 99;
    @o5r3.widget = 1;
    @o5r3.min = 0;
    @o5r3.max = 99;
    @o5r3.step = 1;
    @o5r3.label = "Rate 3";
    @o5r3.group = "Op 5";

    @o5r4 = 70;
    @o5r4.widget = 1;
    @o5r4.min = 0;
    @o5r4.max = 99;
    @o5r4.step = 1;
    @o5r4.label = "Rate 4";
    @o5r4.group = "Op 5";

    @o5l1 = 99;
    @o5l1.widget = 1;
    @o5l1.min = 0;
    @o5l1.max = 99;
    @o5l1.step = 1;
    @o5l1.label = "Level 1";
    @o5l1.group = "Op 5";

    @o5l2 = 99;
    @o5l2.widget = 1;
    @o5l2.min = 0;
    @o5l2.max = 99;
    @o5l2.step = 1;
    @o5l2.label = "Level 2";
    @o5l2.group = "Op 5";

    @o5l3 = 99;
    @o5l3.widget = 1;
    @o5l3.min = 0;
    @o5l3.max = 99;
    @o5l3.step = 1;
    @o5l3.label = "Level 3";
    @o5l3.group = "Op 5";

    @o5l4 = 0;
    @o5l4.widget = 1;
    @o5l4.min = 0;
    @o5l4.max = 99;
    @o5l4.step = 1;
    @o5l4.label = "Level 4";
    @o5l4.group = "Op 5";

    @o6ratio = 4;
    @o6ratio.widget = 1;
    @o6ratio.min = 0.5;
    @o6ratio.max = 31;
    @o6ratio.label = "Ratio";
    @o6ratio.group = "Op 6";

    @o6det = 0;
    @o6det.widget = 1;
    @o6det.min = -50;
    @o6det.max = 50;
    @o6det.label = "Detune (cents)";
    @o6det.group = "Op 6";

    @o6level = 80;
    @o6level.widget = 1;
    @o6level.min = 0;
    @o6level.max = 99;
    @o6level.step = 1;
    @o6level.label = "Level";
    @o6level.group = "Op 6";

    @o6vel = 0;
    @o6vel.widget = 1;
    @o6vel.min = 0;
    @o6vel.max = 7;
    @o6vel.step = 1;
    @o6vel.label = "Velocity";
    @o6vel.group = "Op 6";

    @o6r1 = 99;
    @o6r1.widget = 1;
    @o6r1.min = 0;
    @o6r1.max = 99;
    @o6r1.step = 1;
    @o6r1.label = "Rate 1";
    @o6r1.group = "Op 6";

    @o6r2 = 99;
    @o6r2.widget = 1;
    @o6r2.min = 0;
    @o6r2.max = 99;
    @o6r2.step = 1;
    @o6r2.label = "Rate 2";
    @o6r2.group = "Op 6";

    @o6r3 = 99;
    @o6r3.widget = 1;
    @o6r3.min = 0;
    @o6r3.max = 99;
    @o6r3.step = 1;
    @o6r3.label = "Rate 3";
    @o6r3.group = "Op 6";

    @o6r4 = 70;
    @o6r4.widget = 1;
    @o6r4.min = 0;
    @o6r4.max = 99;
    @o6r4.step = 1;
    @o6r4.label = "Rate 4";
    @o6r4.group = "Op 6";

    @o6l1 = 99;
    @o6l1.widget = 1;
    @o6l1.min = 0;
    @o6l1.max = 99;
    @o6l1.step = 1;
    @o6l1.label = "Level 1";
    @o6l1.group = "Op 6";

    @o6l2 = 99;
    @o6l2.widget = 1;
    @o6l2.min = 0;
    @o6l2.max = 99;
    @o6l2.step = 1;
    @o6l2.label = "Level 2";
    @o6l2.group = "Op 6";

    @o6l3 = 99;
    @o6l3.widget = 1;
    @o6l3.min = 0;
    @o6l3.max = 99;
    @o6l3.step = 1;
    @o6l3.label = "Level 3";
    @o6l3.group = "Op 6";

    @o6l4 = 0;
    @o6l4.widget = 1;
    @o6l4.min = 0;
    @o6l4.max = 99;
    @o6l4.step = 1;
    @o6l4.label = "Level 4";
    @o6l4.group = "Op 6";

    @feedback = 0;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 7;
    @feedback.step = 1;
    @feedback.label = "Feedback";

    @ratescale = 0;
    @ratescale.widget = 1;
    @ratescale.min = 0;
    @ratescale.max = 7;
    @ratescale.step = 1;
    @ratescale.label = "Rate Scaling";

    @mix = 0.3;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Output";

node ionode {
    channels = 2;
    out0 = mix->out;
    out1 = mix->out;
    play = max(e6->play, max(e5->play, max(e4->play, max(e3->play, max(e2->play, e1->play)))));
};

node freq misc::midi2freq {
    note = ionode->note;
};

node e1 env::dx {
    r1 = @o1r1;
    r2 = @o1r2;
    r3 = @o1r3;
    r4 = @o1r4;
    l1 = @o1l1;
    l2 = @o1l2;
    l3 = @o1l3;
    l4 = @o1l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

node e2 env::dx {
    r1 = @o2r1;
    r2 = @o2r2;
    r3 = @o2r3;
    r4 = @o2r4;
    l1 = @o2l1;
    l2 = @o2l2;
    l3 = @o2l3;
    l4 = @o2l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

node e3 env::dx {
    r1 = @o3r1;
    r2 = @o3r2;
    r3 = @o3r3;
    r4 = @o3r4;
    l1 = @o3l1;
    l2 = @o3l2;
    l3 = @o3l3;
    l4 = @o3l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

node e4 env::dx {
    r1 = @o4r1;
    r2 = @o4r2;
    r3 = @o4r3;
    r4 = @o4r4;
    l1 = @o4l1;
    l2 = @o4l2;
    l3 = @o4l3;
    l4 = @o4l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

node e5 env::dx {
    r1 = @o5r1;
    r2 = @o5r2;
    r3 = @o5r3;
    r4 = @o5r4;
    l1 = @o5l1;
    l2 = @o5l2;
    l3 = @o5l3;
    l4 = @o5l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

node e6 env::dx {
    r1 = @o6r1;
    r2 = @o6r2;
    r3 = @o6r3;
    r4 = @o6r4;
    l1 = @o6l1;
    l2 = @o6l2;
    l3 = @o6l3;
    l4 = @o6l4;
    trigger = ionode->trigger;
    note = ionode->note;
    ratescale = @ratescale;
};

# Op 6 as heard: its sine through its envelope and level.
node s6 math::mul {
    in0 = op6->out;
    in1 = e6->out * pow(2, (@o6level - 99) / 8) * pow(2, @o6vel * (ionode->velocity - 1) * 0.6);
};

node op6 osc::fmop {
    freq = freq->out;
    ratio = @o6ratio * exp2(@o6det / 1200);
    feedback = @feedback / 7;
};

# Op 5 as heard: its sine through its envelope and level.
node s5 math::mul {
    in0 = op5->out;
    in1 = e5->out * pow(2, (@o5level - 99) / 8) * pow(2, @o5vel * (ionode->velocity - 1) * 0.6);
};

node op5 osc::fmop {
    freq = freq->out;
    ratio = @o5ratio * exp2(@o5det / 1200);
};

# Op 4 as heard: its sine through its envelope and level.
node s4 math::mul {
    in0 = op4->out;
    in1 = e4->out * pow(2, (@o4level - 99) / 8) * pow(2, @o4vel * (ionode->velocity - 1) * 0.6);
};

node op4 osc::fmop {
    freq = freq->out;
    ratio = @o4ratio * exp2(@o4det / 1200);
};

# Op 3 as heard: its sine through its envelope and level.
node s3 math::mul {
    in0 = op3->out;
    in1 = e3->out * pow(2, (@o3level - 99) / 8) * pow(2, @o3vel * (ionode->velocity - 1) * 0.6);
};

node op3 osc::fmop {
    freq = freq->out;
    ratio = @o3ratio * exp2(@o3det / 1200);
};

# Op 2 as heard: its sine through its envelope and level.
node s2 math::mul {
    in0 = op2->out;
    in1 = e2->out * pow(2, (@o2level - 99) / 8) * pow(2, @o2vel * (ionode->velocity - 1) * 0.6);
};

node op2 osc::fmop {
    freq = freq->out;
    ratio = @o2ratio * exp2(@o2det / 1200);
};

# Op 1 as heard: its sine through its envelope and level.
node s1 math::mul {
    in0 = op1->out;
    in1 = e1->out * pow(2, (@o1level - 99) / 8) * pow(2, @o1vel * (ionode->velocity - 1) * 0.6);
};

node op1 osc::fmop {
    freq = freq->out;
    ratio = @o1ratio * exp2(@o1det / 1200);
};

node mix math::mul {
    in0 = s1->out + s2->out + s3->out + s4->out + s5->out + s6->out;
    in1 = @mix / 6;
};

io ionode;
