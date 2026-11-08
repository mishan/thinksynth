# Rings -- concentric rings from the middle of the picture, moving outward,
# as many as the note is high, and as bright as the music is loud.
#
# `@in_level' sets the brightness, scaled by `Response': a quiet passage
# is a dim glow, a loud one blooms. The rings are copper and their gaps
# violet, from the same wave with the sign turned over.

name "Rings";
author "Misha Nasledov";
description "Rings from the middle, as many as the note is high, as bright as it is loud.";
category "Video";

    @in_level = 0;

    @response = 6;
    @response.widget = 1;
    @response.min = 0;
    @response.max = 16;
    @response.label = "Response";

    @speed = 4;
    @speed.widget = 1;
    @speed.min = 0;
    @speed.max = 16;
    @speed.label = "Speed";

node ionode {
    channels = 3;
    out0 = r->out;
    out1 = g->out;
    out2 = b->out;
    play = env->play;
};

node s misc::scan { };
node f misc::midi2freq { note = ionode->note; };
node env env::adsr { a = 10 ms; d = 100 ms; s = 1; r = 500 ms; p = 1;
                     trigger = ionode->trigger; };

# The square of the distance from the middle, the width stretched for a
# 4:3 picture so the rings come out round.
node ring math::sin {
    index = ((s->x - 0.5) * (s->x - 0.5) * 1.78 + (s->y - 0.5) * (s->y - 0.5)) *
            f->out * 0.2 - s->t * @speed;
    wavelength = 1;
};

node lit math::mul { in0 = ring->out * env->out; in1 = @in_level * @response; };
node r math::mul { in0 = lit->out; in1 = 1; };
node g math::mul { in0 = lit->out; in1 = 0.5; };
node b math::mul { in0 = lit->out; in1 = -0.8; };

io ionode;
