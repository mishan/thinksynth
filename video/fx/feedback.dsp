# Feedback -- the picture fed back into itself a frame late, as a camera
# pointed at its own monitor: everything drawn leaves a trail that drifts
# and fades.
#
# A video synth's effect, for genwav's --video-fx. Each of R, G and B goes
# through a delay::echo a frame long (misc::scan's `size') and `Drift Down'
# lines and `Drift Right' pixels more, so what was drawn comes round a
# little down and to the right of where it was, wrapping at the edges. `Keep' is how much of the trail each frame keeps; past about 0.95 a
# trail outlasts the piece. `Mix' is how much of the live picture is laid
# over it.
#
# What comes in is held at 0 and up first: a patch's negative half is
# black on screen, but fed back it would be a dark trail eating whatever
# it drifts into.

name "Feedback";
author "Misha Nasledov";
description "Video feedback: the picture a frame late, drifting and fading.";
category "Video";

    @keep = 0.88;
    @keep.widget = 1;
    @keep.min = 0;
    @keep.max = 0.98;
    @keep.label = "Keep";

    @down = 1;
    @down.widget = 1;
    @down.min = 0;
    @down.max = 8;
    @down.step = 1;
    @down.label = "Drift Down";

    @right = 1;
    @right.widget = 1;
    @right.min = 0;
    @right.max = 8;
    @right.step = 1;
    @right.label = "Drift Right";

    @mix = 0.5;
    @mix.widget = 1;
    @mix.min = 0;
    @mix.max = 1;
    @mix.label = "Mix";

node ionode {
    channels = 3;
    in0 = 0;
    in1 = 0;
    in2 = 0;
    out0 = r->out;
    out1 = g->out;
    out2 = b->out;
};

node s misc::scan { };

node rin math::max { in0 = ionode->in0; in1 = 0; };

node r delay::echo {
    in = rin->out;
    size = s->size * 2;
    delay = s->size + @down * s->width + @right;
    feedback = @keep;
    dry = @mix;
};

node gin math::max { in0 = ionode->in1; in1 = 0; };

node g delay::echo {
    in = gin->out;
    size = s->size * 2;
    delay = s->size + @down * s->width + @right;
    feedback = @keep;
    dry = @mix;
};

node bin math::max { in0 = ionode->in2; in1 = 0; };

node b delay::echo {
    in = bin->out;
    size = s->size * 2;
    delay = s->size + @down * s->width + @right;
    feedback = @keep;
    dry = @mix;
};

io ionode;
