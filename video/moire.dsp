# Moire -- each note a field of fine rings around a center that wanders
# slowly, finer than the pixels can hold, so what shows is the moire
# between the rings and the raster; a chord's fields interfere.
#
# The note's frequency sets how fine the rings are, and where its center
# starts; the centers drift over tens of seconds. The color runs from blue
# to copper as a center drifts across the picture. `@in_level', the
# music's loudness, brightens it, scaled by `Response'.

name "Moire";
author "Misha Nasledov";
description "Fine rings per note, wandering, interfering into moire.";
category "Video";

    @in_level = 0;

    @fineness = 0.6;
    @fineness.widget = 1;
    @fineness.min = 0.05;
    @fineness.max = 2;
    @fineness.label = "Fineness";

    @response = 2;
    @response.widget = 1;
    @response.min = 0;
    @response.max = 8;
    @response.label = "Response";

node ionode {
    channels = 3;
    out0 = r->out;
    out1 = g->out;
    out2 = b->out;
    play = env->play;
};

node s misc::scan { };
node f misc::midi2freq { note = ionode->note; };
node env env::adsr { a = 300 ms; d = 500 ms; s = 0.8; r = 1200 ms; p = 1;
                     trigger = ionode->trigger; };

# Where the rings center, as a sine of time each way: slow, and started
# somewhere the note chooses.
node cx math::sin { index = s->t * 0.07 + f->out * 0.013; wavelength = 1; amp = 0.25; };
node cy math::sin { index = s->t * 0.05 + f->out * 0.029; wavelength = 1; amp = 0.2; };

# 1.78 is (4/3)^2: the width stretched so the rings are round on a 4:3
# picture.
node ring math::sin {
    index = ((s->x - 0.5 - cx->out) * (s->x - 0.5 - cx->out) * 1.78 +
             (s->y - 0.5 - cy->out) * (s->y - 0.5 - cy->out)) * f->out * @fineness;
    wavelength = 1;
};

node lit math::mul { in0 = ring->out * ring->out * env->out;
                     in1 = 0.25 + @in_level * @response; };
node r math::mul { in0 = lit->out; in1 = 0.5 + 2 * cx->out; };
node g math::mul { in0 = lit->out; in1 = 0.45; };
node b math::mul { in0 = lit->out; in1 = 0.5 - 2 * cx->out; };

io ionode;
