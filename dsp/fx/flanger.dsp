# Flanger -- a comb with its own output in it, swept.
#
# The chorus graph with two things changed: the line is short, and the
# tap goes back into it. `delay::chorus' with `feedback' is a resonant
# comb, and a resonant comb whose delay moves between one and a few
# milliseconds is the jet flying over -- the same node, the same line,
# and a different sound entirely because the peaks are sharp enough to
# hear moving.
#
# WHY THE DELAY IS SHORT. The comb's teeth sit at multiples of
# 1/`Delay': at 1.5 ms they are 667 Hz apart, so there are a dozen of
# them across the spectrum and sweeping them is a pitch. At the 12 ms a
# chorus uses they are 83 Hz apart, so there are hundreds, and what
# sweeps is a texture rather than a note. That is the whole difference
# between the two effects, and it is one knob.
#
# `Feedback' IS SIGNED, and both signs are worth having. Positive puts
# the comb's peaks at multiples of 1/`Delay' -- a harmonic series, which
# reads as a pitch sweeping. Negative inverts what goes back into the
# line, which moves the peaks to the odd multiples of 1/(2*`Delay') and
# leaves a hole where the fundamental was: the hollow, nasal one, which
# is the flanger on most records where you can hear it happening.
#
# THROUGH ZERO is the option a digital flanger has to be given on
# purpose. On tape the effect came from two decks running the same
# recording with a thumb on one reel, and the thumb could make either
# deck the late one -- so the delay between the two copies swept down to
# nothing, crossed, and came back, and at the crossing every notch in
# the comb slid down to DC and there was no comb at all for an instant.
# A delay line cannot read a negative delay, so the only way to get
# there is to delay the DRY copy by the same `Delay' the wet one sits
# at: the moving tap then runs from `Delay' - `Depth' to `Delay' +
# `Depth' around a dry copy that is fixed at `Delay', and the difference
# between them passes through zero twice a cycle. `Mix' at a half is
# what makes the notches infinite, because two copies only cancel when
# they are the same size.
#
# Off, the dry copy is the input where it arrived, the sweep never
# crosses, and what comes out is the ordinary flanger. It is the default:
# through zero is a stronger effect than most pieces want under
# everything, and it costs a line.
#
# KEEP `Depth' AT OR UNDER `Delay'. The tap cannot read less than one
# sample back, so a swing deeper than the delay flattens against the
# write head for part of every cycle -- which is a sound, but not this
# one, and it is not a null.
#
# Two nodes, one a side, whose LFOs sit half a cycle apart: fx/chorus.dsp
# says why that is the whole of the stereo. One tap each, not two -- a
# flanger is one reflection and one comb, and a second tap would be a
# second comb a little out of step with it, which fills the notches in.

name "Flanger";
author "Misha Nasledov";
description "A swept resonant comb for a channel, with a through-zero option.";

    @rate = 0.3;
    @rate.widget = 1;
    @rate.min = 0.02;
    @rate.max = 4;
    @rate.label = "Rate (Hz)";

    # Where the comb sits when the LFO is at zero, and so how far apart
    # its teeth are. See the head: this is the knob that separates a
    # flanger from a chorus.
    @delay = 1.5 ms;
    @delay.widget = 1;
    @delay.min = 0.2 ms;
    @delay.max = 8 ms;
    @delay.label = "Delay";

    @depth = 1.4 ms;
    @depth.widget = 1;
    @depth.min = 0.1 ms;
    @depth.max = 8 ms;
    @depth.label = "Depth";

    @feedback = 0.7;
    @feedback.widget = 1;
    @feedback.min = -0.95;
    @feedback.max = 0.95;
    @feedback.label = "Feedback";

    @through = 0;
    @through.widget = 1;
    @through.min = 0;
    @through.max = 1;
    @through.step = 1;
    @through.values = "Off,On";
    @through.label = "Through Zero";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    # The engine writes these every window; a file declares them so the
    # nodes below have something to read. See fx/echo.dsp.
    in0 = 0;
    in1 = 0;

    out0 = outl->out;
    out1 = outr->out;
};

# The moving tap, and nothing else: `mix = 1' so the node hands back the
# comb alone and the dry is mixed below, where the through-zero copy can
# take its place.
node wetl delay::chorus {
    in = ionode->in0;
    rate = @rate;
    depth = @depth;
    delay = @delay;
    feedback = @feedback;
    taps = 1;
    mix = 1;
    phase = 0;
};

node wetr delay::chorus {
    in = ionode->in1;
    rate = @rate;
    depth = @depth;
    delay = @delay;
    feedback = @feedback;
    taps = 1;
    mix = 1;
    phase = 0.5;
};

# The dry copy, delayed by the tap's center. `gain = 0' on an allpass is
# a plain delay of `delay' samples -- the cheapest line in the tree that
# does nothing but wait.
node fixl delay::allpass {
    in = ionode->in0;
    delay = @delay;
    gain = 0;
};

node fixr delay::allpass {
    in = ionode->in1;
    delay = @delay;
    gain = 0;
};

# Which dry copy this is: the input as it arrived, or that one. A switch
# and not a blend -- `Through Zero' steps -- but the fade is what makes
# it one, and it is the same node either way.
node dryl mixer::fade {
    in0 = ionode->in0;
    in1 = fixl->out;
    fade = @through;
};

node dryr mixer::fade {
    in0 = ionode->in1;
    in1 = fixr->out;
    fade = @through;
};

node outl mixer::fade {
    in0 = dryl->out;
    in1 = wetl->out;
    fade = @mix;
};

node outr mixer::fade {
    in0 = dryr->out;
    in1 = wetr->out;
    fade = @mix;
};

io ionode;
