# Resynth -- the channel's pitch and loudness played on an FM voice.
#
# Not a filter on the audio: the audio is thrown away. What is kept is
# what the channel is *doing* -- how high, how loud, how bright -- and
# two FM operators are played with it. The MS-20's external signal
# processor and the guitar synths that followed it are this machine, and
# what they are for is putting a part through an instrument that cannot
# play it: a bass line on a bell, a drum loop on a saw.
#
# It is therefore the one effect where `Mix' at 1 is a different
# instrument rather than a treated one, and where turning `Mix' down is
# the usual way to use it -- the synth doubling the part it is following
# rather than replacing it.
#
# TRACKING THE PITCH IS THE WHOLE PROBLEM, and `analysis::pitch' counts
# rising zero crossings and does nothing else: no window, no hysteresis.
# Hand it a saw and every harmonic that crosses zero is another edge, so
# it reads high -- straight off `guitar.dsp' at 110 Hz it answers 630.
#
# So `Detect' is a low-pass in front of it, and it is the knob that
# decides whether this works at all. At the default of 150 Hz, against
# five instruments played at five octaves:
#
#     played        55     110     220     440     880 Hz
#     guitar        55.1   110.2   221.6   445.5   900.0
#     epiano       101.6   110.0   220.5   445.5   900.0
#     supersaw      55.0   110.5   221.6   432.4   450.0
#     ts1           55.3   111.6   217.2  1696.2  2000.0
#     bass         176.4    55.1   110.2   222.7   531.3
#
# Three octaves inside about forty cents for most of them, and then each
# one gives up somewhere. The readings that are a few cents sharp are
# the method rather than the filter: a period is a whole number of
# samples, and at 440 Hz the choice is between 99 and 100 of them.
#
# ONE POLE AND NOT TWO, which is not what it sounds like it should be.
# Scored over four instruments at four registers, a single `filt::svf'
# at 150 Hz lands 14 of 16 readings inside fifty cents; two poles of it
# land 13. The second pole takes the fundamental down with the
# harmonics, and the tracker would rather have a quiet fundamental than
# a clean nothing.
#
# `Detect' moves which register works, and that is what it is for. `ts1'
# above fails over 220 Hz at the default; turning the knob up fixes it
# and costs the bottom end nothing:
#
#     played        110     220     440     880 Hz
#     Detect 150   111.6   217.2  1696.2  2000.0
#     Detect 400   111.4   221.6   441.0   612.5
#     Detect 700   111.1   221.6   445.5   882.0
#
# TWO THINGS IT IS RIGHT ABOUT AND SOUND WRONG. Fed `bass.dsp' it tracks
# 55 Hz under a note played at 110, because that file has a sub
# oscillator an octave down and 55 Hz is really there. Fed `supersaw.dsp'
# it holds up until the top octave and then answers 450 for 880, because
# seven detuned saws have no one period to find. Neither is the tracker
# failing; both are a monophonic question asked of something that is not.
#
# `Gate' does two jobs at once: it keeps the tracker off the noise floor
# between notes, where there is nothing to find and it would find
# something anyway, and it shuts the voice up. It is the first knob to
# set. It is a gain rather than the named gate node, for a reason the
# node it replaces carries.
#
# `Glide' is `misc::slew' on the tracked frequency. The tracker reports
# one number per period and a wrong one now and then, and without the
# lag every one of those is a click. It is also the portamento, which is
# what a guitar synth is loved and hated for.
#
# THE VOICE is `osc::fmop' twice, the modulator at `Ratio' times the
# tracked pitch and the carrier at it. `Feedback' is the modulator's own
# output into its own phase -- the thing the graph this replaces tried
# to write as `fm = osc1->out', which is a cycle the format refuses and
# the operator has an arg for.
#
# `Track' is the part that makes it sound played rather than triggered.
# The pre-filter's own high output is everything the tracker was told to
# ignore, which is exactly a brightness reading, and it costs no node to
# take: an averaged amount of it opens the voice's filter. A hard note
# comes out bright and a soft one dull, following the part rather than
# one envelope shape per note.
#
# MONO, and out to both sides. One pitch, one pair of operators: there is
# no second answer to give the other speaker, and two trackers
# disagreeing about which note it is would be worse than one being wrong.

name "Resynth";
author "Misha Nasledov";
description "Tracks a channel's pitch and level and plays them on two FM operators.";

    @detect = 150;
    @detect.widget = 1;
    @detect.min = 60;
    @detect.max = 1200;
    @detect.label = "Detect (Hz)";

    @gate = 0.02;
    @gate.widget = 1;
    @gate.min = 0;
    @gate.max = 0.2;
    @gate.label = "Gate";

    @glide = 900;
    @glide.widget = 1;
    @glide.min = 0;
    @glide.max = 8000;
    @glide.label = "Glide";

    @speed = 2.2;
    @speed.widget = 1;
    @speed.min = 1;
    @speed.max = 4;
    @speed.label = "Response";

    @ratio = 2;
    @ratio.widget = 1;
    @ratio.min = 0.25;
    @ratio.max = 8;
    @ratio.label = "Ratio";

    @index = 3;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 12;
    @index.label = "FM Depth";

    @feedback = 0.2;
    @feedback.widget = 1;
    @feedback.min = 0;
    @feedback.max = 1;
    @feedback.label = "Feedback";

    @tone = 800;
    @tone.widget = 1;
    @tone.min = 200;
    @tone.max = 8000;
    @tone.label = "Tone (Hz)";

    @track = 3000;
    @track.widget = 1;
    @track.min = 0;
    @track.max = 12000;
    @track.label = "Track (Hz)";

    @level = 1;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 8;
    @level.label = "Level";

    @mix = 1;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = out->out;
    out1 = out->out;
};

# How loud the channel is, from both sides summed: everything
# downstream of here is monophonic. See the head.
node level env::follower {
    in = (ionode->in0 + ionode->in1) * 0.5;
    falloff = @speed;
};

# The gate, as a gain and deliberately not as `misc::noisegate'. A gate
# that zeroes its output puts a step in the waveform every time it
# shuts, and a step is a rising edge the tracker counts: measured on
# `guitar.dsp', the named node reads 107.6 Hz and 215.1 for notes at 110
# and 220, where the gain below reads 110.2 and 221.6. Forty cents is
# audible on an effect whose whole job is the pitch, and multiplying by
# a positive number cannot move a zero crossing at all. The obvious node
# is the wrong one here.
#
# It reaches fully open a fortieth of full scale under `Gate', so there
# is a knee rather than a click, and at `Gate = 0' the expression is 1
# plus a positive number and the clamp holds it open however quiet the
# channel goes.
node open math::clamp {
    in = 1 - (@gate - level->out) * 40;
    lo = 0;
    hi = 1;
};

node gate mixer::mul {
    in0 = (ionode->in0 + ionode->in1) * 0.5;
    in1 = open->out;
};

# One filter, both answers: `out_low' is what the tracker is allowed to
# count and `out_high' is everything it was told to ignore, which is a
# brightness reading for free. See the head for why one pole and not
# two.
node pre filt::svf {
    in = gate->out;
    cutoff = @detect;
    res = 0;
};

node found analysis::pitch {
    in = pre->out_low;
};

# The tracker answers 0 until it has measured a period, and after a
# silence its first answer is the length of that silence -- a frequency
# near nothing. Neither is a note. The clamp keeps the operators inside
# the range the tracker is good for, and the level envelope is what
# actually decides whether any of it is heard.
node held math::clamp {
    in = found->out;
    lo = 30;
    hi = 2000;
};

node pitch misc::slew {
    in = held->out;
    time = @glide;
};

# The voice's loudness: what the channel is doing, shut by the same
# gain that shuts the tracker, so `Gate' closes both.
node amp mixer::mul {
    in0 = level->out;
    in1 = open->out;
};

node bright env::followavg {
    in = pre->out_high;
    falloff = @speed;
};

node mod osc::fmop {
    freq = pitch->out;
    ratio = @ratio;
    feedback = @feedback;
};

node car osc::fmop {
    freq = pitch->out;
    ratio = 1;
    mod = mod->out;
    index = @index;
};

node filt filt::svf {
    in = car->out;
    cutoff = @tone + @track * bright->out;
    res = 0;
};

node voice mixer::mul {
    in0 = filt->out_low * @level;
    in1 = amp->out;
};

node out mixer::fade {
    in0 = (ionode->in0 + ionode->in1) * 0.5;
    in1 = voice->out;
    fade = @mix;
};

io ionode;
