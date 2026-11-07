# Reverse Cymbal -- the kit's ride, played back to front.
#
# osc::sample with `reverse' on: the ride's long wash first, quietly,
# rising to the strike, and silence after it. Place the note so its end
# lands on the downbeat it leads into: it lasts as long as the file at
# the pitch it is played, so `Start' trims it from the quiet end to make
# it shorter. Played higher it is shorter and brighter.

name "Reverse Cymbal";
author "Misha Nasledov";
description "The kit's ride played backwards, rising into the beat.";
category "Drums";

    @start = 0 ms;
    @start.widget = 1;
    @start.min = 0ms;
    @start.max = 2300ms;
    @start.label = "Start";

    @level = 0.8;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = out->out;
    out1 = out->out;
    play = smp->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node smp osc::sample {
    file = "kit_ride_hard.wav";
    freq = freq->out;
    root = 261.63;
    start = @start;
    reverse = 1;
    trigger = ionode->trigger;
};

node out math::mul {
    in0 = smp->out;
    in1 = @level * ionode->velocity;
};

io ionode;
