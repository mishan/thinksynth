# Dub -- an echo whose repeats are processed every time they come round.
#
# fx/echo.dsp darkens its tail as a whole, because a graph cannot filter
# what a delay feeds back to itself. delay::echo now does that itself:
# `tone', `low' and `drive' on every lap. So the first repeat is nearly
# the sound that went in, the fifth is a dark, thin smear of it, and
# `Runaway' pushes the loop past unity -- the tail builds instead of
# dying, until the saturation holds it, and comes down only when
# `Runaway' does. That is the mixing desk played as an instrument.
#
# THE TIME IS IN BEATS of the piece's tempo, so a dotted eighth stays one
# when the tempo moves; the right side repeats `Spread' times as far
# apart, so the two sides land between each other.
#
# `Input' is how much of the channel goes into the echo, apart from how
# much of it is heard: at 0 the channel passes dry and the echo only
# plays out what it already holds. A chain with xform::throw rides it
# (`chanarg = "fx.input"') to send one hit in eight and no other -- the
# throw.
#
# `Wow' is a slow wobble on the wet, the capstan of a tape echo; on the
# wet as a whole, for the reason fx/tape.dsp gives.

name "Dub";
author "Misha Nasledov";
description "A dub echo: repeats darkened, thinned and saturated on every lap, timed in beats, and a Runaway that lets the tail build.";
category "Effects";

    @beats = 0.75;
    @beats.widget = 1;
    @beats.min = 0.25;
    @beats.max = 2;
    @beats.label = "Time (beats)";

    @spread = 1.5;
    @spread.widget = 1;
    @spread.min = 0.5;
    @spread.max = 2;
    @spread.label = "Right Time x";

    @feedback = 0.6;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 0.95;
    @feedback.label = "Feedback";

    @tone = 2500;
    @tone.widget = 1;
    @tone.min = 400;
    @tone.max = 12000;
    @tone.label = "Tone (Hz)";

    @low = 200;
    @low.widget = 1;
    @low.min = 20;
    @low.max = 1500;
    @low.label = "Low Cut (Hz)";

    @drive = 1;
    @drive.widget = 1;
    @drive.min = 0;
    @drive.max = 4;
    @drive.label = "Drive";

    @runaway = 0;
    @runaway.widget = 1;
    @runaway.min = 0;
    @runaway.max = 0.5;
    @runaway.label = "Runaway";

    @wow = 0.4 ms;
    @wow.widget = 1;
    @wow.min = 0ms;
    @wow.max = 3ms;
    @wow.label = "Wow";

    @input = 1;
    @input.widget = 1;
    @input.min = 0;
    @input.max = 1;
    @input.label = "Input";

    @mix = 0.35;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

    # The ring. Not a knob: the taps are clamped to it.
    @ring = 8000 ms;

node ionode {
    channels = 2;

    # The engine writes these every window. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

node tempo misc::tempo { };

# `Input' slewed, so a throw opens and closes the echo rather than
# stepping it, which the echo would record as a click and repeat.
node input misc::slew {
    in = @input;
    time = 8 ms;
};

# delay::echo crossfades the input into the ring by 1 - feedback, so the
# input is scaled back up by as much: the first repeat is the sound at its
# own level and each after it `Feedback' of the last, as a desk's is.
# Feedback stops at 0.95, twenty times; at 1 that would divide by zero,
# and past 0.95 is what `Runaway' is for.
node echol delay::echo {
    in = ionode->in0 * input->out / (1 - min(@feedback, 0.95));
    size = @ring;
    delay = min(tempo->beat * @beats, @ring - 1);
    feedback = min(@feedback, 0.95);
    dry = 0;
    tone = @tone;
    low = @low;
    drive = @drive;
    boost = @runaway;
};

node echor delay::echo {
    in = ionode->in1 * input->out / (1 - min(@feedback, 0.95));
    size = @ring;
    delay = min(tempo->beat * @beats * @spread, @ring - 1);
    feedback = min(@feedback, 0.95);
    dry = 0;
    tone = @tone;
    low = @low;
    drive = @drive;
    boost = @runaway;
};

node wowl delay::chorus {
    in = echol->out;
    rate = 0.5;
    depth = @wow;
    delay = 4 ms;
    taps = 1;
    mix = 1;
    phase = 0;
};

node wowr delay::chorus {
    in = echor->out;
    rate = 0.5;
    depth = @wow;
    delay = 4 ms;
    taps = 1;
    mix = 1;
    phase = 0.3;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = wowl->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = wowr->out;
    fade = @mix;
};

io ionode;
