# Hall -- a reverb on a channel's sum, from combs and a lowpass.
#
# Schroeder's arrangement, more or less: four comb filters in parallel
# on each side, their delays chosen to share no factor so the echoes do
# not pile up on one another, summed and darkened by a lowpass, and
# mixed with the dry. The right side's combs are seven percent longer
# than the left's, which is the whole of the stereo. `Decay' is the
# combs' feedback; `Size' scales every delay together, which is the
# difference between a room and a hall. filt::comb takes its delay as a
# frequency, so `Size' divides.
#
# This is an effect graph -- `in0' on the io node -- and it runs on the
# channel's summed voices every window, which is what lets the tail
# outlive the note. See fx/echo.dsp.

name "Hall";
author "Misha Nasledov";
description "A comb-filter reverb for a channel, with a damped tail.";

    @decay = 0.82;
    @decay.widget = 1;
    @decay.min = 0;
    @decay.max = 0.96;
    @decay.label = "Decay";

    @size = 1;
    @size.widget = 1;
    @size.min = 0.4;
    @size.max = 2.5;
    @size.label = "Size";

    @damping = 3600;
    @damping.widget = 1;
    @damping.min = 400;
    @damping.max = 16000;
    @damping.label = "Damping (Hz)";

    @mix = 0.25;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = mixl->out;
    out1 = mixr->out;
};

# Left: 29.7, 37.1, 41.1 and 43.7 milliseconds at size 1, as hertz.
node cl0 filt::comb { in = ionode->in0; freq = 33.7 / @size; feedback = @decay; size = 500 ms; };
node cl1 filt::comb { in = ionode->in0; freq = 27.0 / @size; feedback = @decay; size = 500 ms; };
node cl2 filt::comb { in = ionode->in0; freq = 24.3 / @size; feedback = @decay; size = 500 ms; };
node cl3 filt::comb { in = ionode->in0; freq = 22.9 / @size; feedback = @decay; size = 500 ms; };

# Right: the same four, seven percent longer.
node cr0 filt::comb { in = ionode->in1; freq = 31.5 / @size; feedback = @decay; size = 500 ms; };
node cr1 filt::comb { in = ionode->in1; freq = 25.2 / @size; feedback = @decay; size = 500 ms; };
node cr2 filt::comb { in = ionode->in1; freq = 22.7 / @size; feedback = @decay; size = 500 ms; };
node cr3 filt::comb { in = ionode->in1; freq = 21.4 / @size; feedback = @decay; size = 500 ms; };

node dampl filt::svf {
    in = (cl0->out + cl1->out + cl2->out + cl3->out) * 0.25;
    cutoff = @damping;
    res = 0;
};

node dampr filt::svf {
    in = (cr0->out + cr1->out + cr2->out + cr3->out) * 0.25;
    cutoff = @damping;
    res = 0;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = dampl->out_low;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = dampr->out_low;
    fade = @mix;
};

io ionode;
