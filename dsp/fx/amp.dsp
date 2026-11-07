# Amp -- two valve stages, a tone stack and a speaker cabinet.
#
# Most of an electric guitar's sound is what happens after the string,
# and `guitar.dsp' stops at the string. This is the rest of the chain,
# in the order an amplifier puts it.
#
# THE INPUT IS TIGHTENED FIRST. A high-pass at 100 Hz in front of the
# gain, because low end driven into a valve turns to mud: an amp that
# keeps the bass out of the preamp and puts it back in the tone stack
# is how a high-gain chord stays a chord.
#
# TWO STAGES, AND THE FIRST IS BIASED. A tanh is symmetric and makes
# odd harmonics only; a valve is not, because it is biased off center,
# and the even harmonics that asymmetry adds are the warmth people mean
# by "tube". So stage one is tanh of the signal plus a bias, less tanh
# of the bias alone, which is zero for silence; a low-pass at
# 6 kHz between the stages takes the fizz out before stage two clips
# it again, symmetrically. `Gain' drives both.
#
# THE TONE STACK is one state-variable filter at 700 Hz, its three
# outputs -- which sum to the input -- weighted by `Bass', `Mid' and
# `Treble'. At 5 a knob is flat and each step either side is about
# 2.4 dB, so all three at 5 is the signal unchanged. It sits after the
# gain, where an amp has it.
#
# THE CABINET is what makes it a guitar amp rather than a fuzz. A
# 12-inch speaker passes nothing much below 80 Hz -- where the box
# resonates, hence the bump -- and nothing above 5 kHz, with a peak
# where the cone breaks up just under that. Two low-passes give the
# 24 dB slope; `Cab' at 0 bypasses it for a direct, harsh box.
#
# Mono in the middle, because an amp is: the two sides are summed in.
#
# AND AN ECHO PEDAL, because a channel carries one effect and a lead
# guitar without its delay is not the sound anybody means. It hangs off
# the cabinet, left at `Delay' and right half again as late, which is
# where the stereo comes from; `Echo' at 0 is off.

name "Amp";
author "Misha Nasledov";
description "A guitar amplifier: a tightened input, two valve stages, a three-band tone stack and a 12-inch cabinet.";
category "Effects";

    @gain = 5;
    @gain.widget = 1;
    @gain.min = 0;
    @gain.max = 10;
    @gain.label = "Gain";

    @bass = 5;
    @bass.widget = 1;
    @bass.min = 0;
    @bass.max = 10;
    @bass.label = "Bass";
    @bass.group = "Tone";

    @mid = 5;
    @mid.widget = 1;
    @mid.min = 0;
    @mid.max = 10;
    @mid.label = "Mid";
    @mid.group = "Tone";

    @treble = 5;
    @treble.widget = 1;
    @treble.min = 0;
    @treble.max = 10;
    @treble.label = "Treble";
    @treble.group = "Tone";

    @bias = 0.2;
    @bias.widget = 1;
    @bias.min = 0;
    @bias.max = 0.5;
    @bias.label = "Bias";

    @cab = 1;
    @cab.widget = 1;
    @cab.min = 0;
    @cab.max = 1;
    @cab.label = "Cab";

    @echo = 0;
    @echo.widget = 1;
    @echo.min = 0;
    @echo.max = 1;
    @echo.label = "Echo";
    @echo.group = "Echo";

    @delay = 375 ms;
    @delay.widget = 1;
    @delay.min = 20 ms;
    @delay.max = 1000 ms;
    @delay.label = "Delay";
    @delay.group = "Echo";

    @feedback = 0.35;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 0.9;
    @feedback.label = "Feedback";
    @feedback.group = "Echo";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 4;
    @level.label = "Level";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = out->out + echol->out * @echo;
    out1 = out->out + echor->out * @echo;
};

node tight filt::svf {
    in = (ionode->in0 + ionode->in1) * 0.5;
    cutoff = 100;
    res = 0;
};

node stage1 dist::saturate {
    in = tight->out_high * (1 + @gain * 3) + @bias;
    factor = 1;
};

node rest dist::saturate {
    in = @bias;
    factor = 1;
};

node couple filt::svf {
    in = stage1->out - rest->out;
    cutoff = 6000;
    res = 0;
};

node stage2 dist::saturate {
    in = couple->out_low;
    factor = 1 + @gain * 0.5;
};

node stack filt::svf {
    in = stage2->out;
    cutoff = 700;
    res = 0.2;
};

node tone math::add {
    in0 = stack->out_low * exp2((@bass - 5) * 0.4) +
          stack->out_band * exp2((@mid - 5) * 0.4);
    in1 = stack->out_high * exp2((@treble - 5) * 0.4);
};

# The box: a resonant high-pass at 85 Hz, then two low-passes, the first
# with the cone's breakup peak on it.
node box filt::svf {
    in = tone->out;
    cutoff = 85;
    res = 0.4;
};

node cone filt::svf {
    in = box->out_high;
    cutoff = 4200;
    res = 0.5;
};

node cone2 filt::svf {
    in = cone->out_low;
    cutoff = 5000;
    res = 0;
};

node cabmix mixer::fade {
    in0 = tone->out;
    in1 = cone2->out_low;
    fade = @cab;
};

# A driven stage comes out near full scale however hard it was hit, so
# the master is divided by the gain: `Gain' adds dirt, `Level' adds
# volume, and a quiet clean channel stays as loud as it went in.
node out mixer::mul {
    in0 = cabmix->out;
    in1 = @level / (1 + @gain * 2);
};

node echol delay::echo {
    in = out->out;
    size = 1600 ms;
    delay = @delay;
    feedback = @feedback;
    dry = 0;
};

node echor delay::echo {
    in = out->out;
    size = 1600 ms;
    delay = @delay * 1.5;
    feedback = @feedback;
    dry = 0;
};

io ionode;
