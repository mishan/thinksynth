# Two Wells -- each note two bright points circling the middle slowly,
# with bands around them where their pull is the same: rounds about each
# one, merging into a figure eight and wider loops around both. Near a
# point the bands are finer than the pixels can hold, and what shows is
# moire; a chord's pairs, turned apart by their pitches, interfere.
#
# One point is copper and the other blue, and a band takes the color of
# the one that pulls it harder. `Fineness' is how close the bands are,
# `Drift' how fast the pairs turn. `@in_level', the music's loudness,
# brightens it, scaled by `Response'.

name "Two Wells";
author "Misha Nasledov";
description "Two points per note, circling, with moire bands where their pull is equal.";
category "Video";

    @in_level = 0;

    @fineness = 1;
    @fineness.widget = 1;
    @fineness.min = 0.3;
    @fineness.max = 3;
    @fineness.label = "Fineness";

    @drift = 1;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 4;
    @drift.label = "Drift";

    @response = 2;
    @response.widget = 1;
    @response.min = 0;
    @response.max = 12;
    @response.label = "Response";

node ionode {
    channels = 3;
    out0 = r->out;
    out1 = g->out;
    out2 = b->out;
    play = env->play;
};

node s misc::scan { };
node env env::adsr { a = 400 ms; d = 800 ms; s = 0.8; r = 1500 ms; p = 1;
                     trigger = ionode->trigger; };

# The pair's turn: a semitone a twelfth of the way round, so a chord's
# pairs fan out, all turning together over a minute or so. A low note's
# points stand wider apart.
node high math::clamp { in = (ionode->note - 36) / 48; lo = 0; hi = 1; };
node ux math::sin { index = s->t * 0.015 * @drift + ionode->note / 12 + 0.25; };
node uy math::sin { index = s->t * 0.015 * @drift + ionode->note / 12; };
node sep math::mul { in0 = 0.1 + 0.15 * (1 - high->out); in1 = 1; };

# The squared distances to the two points, the pair standing at +-sep*u
# from the middle: the sum and the difference of the same two terms.
node ax math::mul { in0 = s->x - 0.5; in1 = 1.33; };
node ay math::sub { in0 = s->y; in1 = 0.5; };
node base math::add { in0 = ax->out * ax->out + ay->out * ay->out; in1 = sep->out * sep->out; };
node dot math::mul { in0 = 2 * sep->out; in1 = ax->out * ux->out + ay->out * uy->out; };
node d1 math::sub { in0 = base->out; in1 = dot->out; };
node d2 math::add { in0 = base->out; in1 = dot->out; };

# Each point's pull, falling off with the square of the distance and
# held finite near the point, where bands any closer would be noise.
node p1 math::div { in0 = 1; in1 = d1->out + 0.01; };
node p2 math::div { in0 = 1; in1 = d2->out + 0.01; };
node pull math::add { in0 = p1->out; in1 = p2->out; };

# The bands creep outward, a band every half minute or so.
node band math::sin { index = pull->out * 0.05 * @fineness - s->t * 0.03 * @drift; };

# Brighter nearer the points, so the far field is a dim frame for them.
node near math::div { in0 = pull->out; in1 = pull->out + 30; };
node lit math::mul {
    in0 = band->out * band->out * near->out * env->out;
    in1 = 0.4 + @in_level * @response;
};

# 1 where the first point pulls harder, 0 where the second does.
node a math::div { in0 = p1->out; in1 = pull->out; };
node r math::mul { in0 = lit->out; in1 = 0.15 + 0.85 * a->out; };
node g math::mul { in0 = lit->out; in1 = 0.45; };
node b math::mul { in0 = lit->out; in1 = 1 - 0.85 * a->out; };

io ionode;
