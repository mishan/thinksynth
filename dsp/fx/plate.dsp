# Plate -- a sheet of steel with a pickup on it, not a room.
#
# fx/hall.dsp is Schroeder's room: four combs a side for the tail, two
# allpasses to spread the echoes, and enough delay in the combs that the
# ear hears a space with walls. A plate reverb is a different machine
# and the differences are all in the numbers:
#
#   SHORTER LINES. A plate is two metres of steel, not thirty of hall,
#   so its modes are close together and its echo density is up at once
#   rather than building. The combs here run at a third of the hall's
#   lengths, which is what puts the density there.
#
#   MORE DIFFUSION. Four allpasses a side instead of two. Steel has no
#   corners, so there is nothing in a plate's response that arrives as a
#   distinct reflection -- if you can count the early echoes it is a
#   room, and a plate's job is to have none.
#
#   BRIGHTER. A plate's damping is a pad of felt against the sheet, and
#   what it takes is the very top rather than everything above a wall's
#   absorption. `Damping' therefore sits at six kilohertz where the
#   hall's sits at three, and that alone is most of what makes this the
#   seventies vocal and snare reverb: the tail is as bright as the hit.
#
#   A PRE-DELAY. The one control a plate has that a room does not need.
#   A room's early reflections arrive with the sound; a plate hung in
#   another building arrives whenever the engineer says, and twenty or
#   thirty milliseconds of gap is what keeps a snare's crack in front of
#   its reverb rather than inside it. `delay::echo' with no feedback is
#   a plain delay line, which is all this is.

name "Plate";
author "Misha Nasledov";
description "A dense, bright reverb with a pre-delay: the plate on a seventies snare.";

    @predelay = 24 ms;
    @predelay.widget = 1;
    @predelay.min = 0;
    @predelay.max = 250ms;
    @predelay.label = "Pre-delay";

    @decay = 0.83;
    @decay.widget = 1;
    @decay.min = 0;
    @decay.max = 0.97;
    @decay.label = "Decay";

    @size = 1;
    @size.widget = 1;
    @size.min = 0.4;
    @size.max = 2;
    @size.label = "Size";

    @damping = 6200;
    @damping.widget = 1;
    @damping.min = 800;
    @damping.max = 16000;
    @damping.label = "Damping (Hz)";

    @diffuse = 190;
    @diffuse.widget = 1;
    @diffuse.min = 20;
    @diffuse.max = 900;
    @diffuse.label = "Diffusion (samples)";

    @mix = 0.3;
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

# The gap. No feedback and no dry, so this is a line and nothing else.
node prel delay::echo {
    in = ionode->in0;
    size = 300 ms;
    delay = @predelay;
    feedback = 0;
    dry = 0;
};

node prer delay::echo {
    in = ionode->in1;
    size = 300 ms;
    delay = @predelay;
    feedback = 0;
    dry = 0;
};

# Left: 9.4, 11.1, 12.6 and 13.9 milliseconds at size 1, as hertz. A
# third of the hall's, which is the difference between a plate and a
# room -- the modes are close enough together that the density is there
# from the first millisecond.
node cl0 filt::comb { in = prel->out; freq = 106.4 / @size; feedback = @decay; size = 200 ms; };
node cl1 filt::comb { in = prel->out; freq =  90.1 / @size; feedback = @decay; size = 200 ms; };
node cl2 filt::comb { in = prel->out; freq =  79.4 / @size; feedback = @decay; size = 200 ms; };
node cl3 filt::comb { in = prel->out; freq =  71.9 / @size; feedback = @decay; size = 200 ms; };

# Right: the same four, nine percent longer, so the two sides never
# agree about where a mode is.
node cr0 filt::comb { in = prer->out; freq =  97.6 / @size; feedback = @decay; size = 200 ms; };
node cr1 filt::comb { in = prer->out; freq =  83.3 / @size; feedback = @decay; size = 200 ms; };
node cr2 filt::comb { in = prer->out; freq =  74.1 / @size; feedback = @decay; size = 200 ms; };
node cr3 filt::comb { in = prer->out; freq =  66.8 / @size; feedback = @decay; size = 200 ms; };

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

# Four a side. Each one takes every echo it is handed and spreads it
# into a run of its own without touching the level of any frequency, so
# four in series is a tail with nothing countable left in it -- which is
# steel. The lengths fall away from each other so that no two stages
# ring at the same spacing.
node apl0 delay::allpass { in = dampl->out_low; delay = @diffuse * @size;        gain = 0.7; };
node apl1 delay::allpass { in = apl0->out;      delay = @diffuse * @size * 0.63; gain = 0.7; };
node apl2 delay::allpass { in = apl1->out;      delay = @diffuse * @size * 0.41; gain = 0.7; };
node apl3 delay::allpass { in = apl2->out;      delay = @diffuse * @size * 0.27; gain = 0.7; };

node apr0 delay::allpass { in = dampr->out_low; delay = @diffuse * @size * 1.09; gain = 0.7; };
node apr1 delay::allpass { in = apr0->out;      delay = @diffuse * @size * 0.69; gain = 0.7; };
node apr2 delay::allpass { in = apr1->out;      delay = @diffuse * @size * 0.45; gain = 0.7; };
node apr3 delay::allpass { in = apr2->out;      delay = @diffuse * @size * 0.29; gain = 0.7; };

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = apl3->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = apr3->out;
    fade = @mix;
};

io ionode;
