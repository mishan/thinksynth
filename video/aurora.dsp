# Aurora -- each note a curtain of light hanging over the picture,
# rippling slowly side to side, streaked with rays: a pink fringe at its
# lower edge, green above, fading up into red and violet. A low note's
# curtain hangs low, a high note's high and redder, as an aurora is
# higher up, and a chord stacks them.
#
# `@in_level_slow', the music's loudness, brightens every curtain, scaled
# by `Response'; `@in_bass_slow' makes them taller. `Drift' is how fast they
# ripple.

name "Aurora";
author "Misha Nasledov";
description "A rippling curtain of light per note: pink fringe, green, then red and violet above.";
category "Video";

    @in_level_slow = 0;
    @in_bass_slow = 0;

    @drift = 1;
    @drift.widget = 1;
    @drift.min = 0;
    @drift.max = 4;
    @drift.label = "Drift";

    @response = 3;
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
node env env::adsr { a = 800 ms; d = 1000 ms; s = 0.8; r = 3000 ms; p = 1;
                     trigger = ionode->trigger; };

# 0 for a low note, 1 for a high one.
node high math::clamp { in = (ionode->note - 36) / 48; lo = 0; hi = 1; };

# The curtain's lower edge, down from the top: a slow ripple whose waves
# shorten across the picture, so no two are alike, started where the note
# chooses.
node w1 math::sin { index = s->x * (0.6 + 1.2 * s->x) + s->t * 0.05 * @drift +
                            ionode->note * 0.13;
                    amp = 0.08; };
node edge math::add { in0 = 0.85 - 0.5 * high->out; in1 = w1->out; };

# The slow inputs, so the curtain swells where the music changes partway
# down a frame rather than being cut straight across there.
node loud math::clamp { in = @in_level_slow * @response; lo = 0; hi = 1; };

# How far above the edge, as a share of the curtain's height: 0 at the
# edge, 1 at the top. Under the edge it falls off within a line or two.
# Over a height that holds still for a window, so its reciprocal is
# worked out once a window rather than divided by each pixel.
node tall math::div { in0 = 1; in1 = 0.3 + @in_bass_slow; };
node up math::mul { in0 = edge->out - s->y; in1 = tall->out; };
node near math::clamp { in = 1 - up->out; lo = 0; hi = 1; };
node bottom math::clamp { in = 1 + up->out * 25; lo = 0; hi = 1; };

# The rays: upright streaks that lean with the ripple and slide along.
node ray math::sin { index = s->x * 23 + w1->out * 30 + up->out * 1.5 +
                         s->t * 0.03 * @drift; };

node lit math::mul {
    in0 = near->out * (0.4 + 0.6 * near->out) * bottom->out * (0.55 + 0.45 * ray->out) * env->out;
    in1 = 0.45 + loud->out;
};

# 0 green, 1 red-violet: higher up the curtain, and higher for a high
# note. The fringe is the pink in the first few lines over the edge.
node hue math::clamp { in = up->out * 1.2 + 0.5 * high->out - 0.2; lo = 0; hi = 1; };
node fringe math::clamp { in = 1 - up->out * 10; lo = 0; hi = 1; };
node r math::mul { in0 = lit->out; in1 = 0.15 + 0.75 * hue->out + 0.6 * fringe->out; };
node g math::mul { in0 = lit->out; in1 = (0.9 - 0.75 * hue->out) * (1 - 0.6 * fringe->out); };
node b math::mul { in0 = lit->out; in1 = 0.3 + 0.2 * hue->out + 0.3 * fringe->out; };

io ionode;
