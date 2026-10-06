# Tape -- an echo that is a machine with a motor in it.
#
# fx/echo.dsp is a delay: a line, a feedback path and a lowpass, and
# every repeat is the last one quieter and duller. A tape echo is a loop
# of oxide running past a head, and three things happen to it that do
# not happen to a delay line.
#
#   IT WOBBLES. The capstan is never exactly on speed, so the loop's
#   length moves and every repeat is a few cents off the one before.
#   That is `Wow', a `delay::chorus' whose taps sit on the wet signal
#   and move it slowly.
#
#   IT SATURATES. Tape run hot compresses and adds harmonics, and a
#   repeat that has been round four times has been through that four
#   times -- which is why a tape echo's tail turns into a dark, fat
#   smear rather than a clean copy getting quieter. `Drive' is that.
#
#   IT DARKENS. Oxide and a head both lose the top, so the tail loses it
#   twice a lap. `Tone' is the lowpass that stands in for both.
#
# WHERE THESE SIT, AND WHY NOT IN THE FEEDBACK PATH. A graph whose node
# reads its own output has a loop, and a graph with a loop runs a sample
# at a time, several times the cost of one without. The wobble and the
# saturation therefore sit on the wet signal as a whole rather than
# inside the loop: every repeat gets the same helping instead of one
# more than the last. fx/echo.dsp's damping makes the same trade for the
# same reason. What is lost is the compounding; what is kept is a cheap
# effect.

name "Tape";
author "Misha Nasledov";
description "A delay with wow, saturation and a lowpass on its repeats: the tape echo.";
category "Effects";

    @delay = 340 ms;
    @delay.widget = 1;
    @delay.min = 20ms;
    @delay.max = 2000ms;
    @delay.label = "Delay";

    @spread = 1.5;
    @spread.widget = 1;
    @spread.min = 0.25;
    @spread.max = 4;
    @spread.label = "Right Delay x";

    @feedback = 0.42;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 0.95;
    @feedback.label = "Repeats";

    @wow = 3;
    @wow.widget = 1;
    @wow.min = 0;
    @wow.max = 20;
    @wow.label = "Wow (samples)";

    @rate = 0.6;
    @rate.widget = 1;
    @rate.min = 0.05;
    @rate.max = 6;
    @rate.label = "Wow Rate (Hz)";

    @drive = 2.2;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 8;
    @drive.label = "Drive";

    @tone = 3200;
    @tone.widget = 1;
    @tone.min = 400;
    @tone.max = 16000;
    @tone.label = "Tone (Hz)";

    @mix = 0.35;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out[] = mix[]->out;
};

# The right side's delay is the left's times `spread'.
node tape[2] delay::echo {
    in = ionode->in[];
    size = 8000 ms;
    delay = @delay * pow(@spread, []);
    feedback = @feedback;
    dry = 0;
};

# The capstan. One tap, full wet, moving slowly: two would be a chorus,
# and a machine has one motor.
node wow[2] delay::chorus {
    in = tape[]->out;
    rate = @rate;
    depth = @wow;
    delay = 30;
    taps = 1;
    mix = 1;
    phase = [] * 0.5;
};

node hot[2] dist::saturate {
    in = wow[]->out;
    factor = @drive;
};

node tone[2] filt::svf {
    in = hot[]->out;
    cutoff = @tone;
    res = 0;
};

node mix[2] mixer::fade {
    in0 = ionode->in[];
    in1 = tone[]->out_low;
    fade = @mix;
};

io ionode;
