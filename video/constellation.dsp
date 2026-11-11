# Constellation -- each note a star, at a place in the sky its pitch
# chooses, that lingers after the note ends: a melody leaves its shape
# behind, and the same tune draws the same constellation.
#
# A star is a soft core and a cross of faint spikes, warm for a low note
# and blue-white for a high one, twinkling slowly. `Size' is how big the
# core is; `Twinkle' how deep the flicker goes, deeper as the music's
# highs are louder. `@in_level_slow', the music's loudness, brightens the
# stars, scaled by `Response'.

name "Constellation";
author "Misha Nasledov";
description "A star per note, placed by its pitch, lingering into a constellation.";
category "Video";

    @in_level_slow = 0;
    @in_high_slow = 0;

    @size = 1;
    @size.widget = 1;
    @size.min = 0.6;
    @size.max = 3;
    @size.label = "Size";

    @twinkle = 0.3;
    @twinkle.widget = 1;
    @twinkle.min = 0;
    @twinkle.max = 1;
    @twinkle.label = "Twinkle";

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
node env env::adsr { a = 40 ms; d = 600 ms; s = 0.6; r = 4000 ms; p = 1;
                     trigger = ionode->trigger; };

# Where the star is. The steps a semitone moves it each way were searched
# for over the piano's range: no two notes land within 3 pixels, and few
# notes within an octave of each other share a column or a row, so a
# chord or a run scatters rather than drawing a line.
node px math::sin { index = ionode->note * 0.862030; amp = 0.42; };
node py math::sin { index = ionode->note * 0.263310 + 0.068533; amp = 0.4; };

# Across in units of the picture's height: 1.33 is its 4:3.
node dx math::mul { in0 = s->x - 0.5 - px->out; in1 = 1.33; };
node dy math::sub { in0 = s->y - 0.5; in1 = py->out; };
node d2 math::add { in0 = dx->out * dx->out; in1 = dy->out * dy->out; };

# The core: a glow whose half-width is `k''s square root, about two
# pixels at Size 1, squared so it falls off quickly, to about one.
node k math::mul { in0 = 0.00025 * @size; in1 = @size; };
node glow math::div { in0 = k->out; in1 = k->out + d2->out; };

# The spikes: bright along the two lines through the star, where dx * dy
# is small, and fading with the distance.
node arms math::abs { in = dx->out * dy->out; };
node cross math::div { in0 = k->out * 0.15; in1 = k->out * 0.15 + arms->out; };
node fade math::div { in0 = 1; in1 = 1 + d2->out * 80; };

# The slow inputs, so a star does not step across its middle where the
# music changes partway down a frame.
node loud math::clamp { in = @in_level_slow * @response; lo = 0; hi = 1; };

# How deep the flicker goes, at most all the way out and never past it
# into negative light.
node depth math::min { in0 = @twinkle * (0.5 + @in_high_slow * 4); in1 = 1; };
node tw math::sin { index = s->t * (0.4 + ionode->note * 0.011); amp = 0.5; };
node lit math::mul {
    in0 = (glow->out * glow->out + 0.5 * cross->out * fade->out) * env->out;
    in1 = (1 - depth->out * (0.5 + tw->out)) * (1 + loud->out);
};

# 0 for a low note, 1 for a high one: the star's color between.
node warm math::clamp { in = (ionode->note - 36) / 48; lo = 0; hi = 1; };
node r math::mul { in0 = lit->out; in1 = 1 - 0.45 * warm->out; };
node g math::mul { in0 = lit->out; in1 = 0.85; };
node b math::mul { in0 = lit->out; in1 = 0.45 + 0.55 * warm->out; };

io ionode;
