# Palette -- the picture recolored by how bright each pixel is, from
# black, by way of a palette's middle color, to its light one: any patch
# in the same few colors.
#
# A video synth's effect, for genwav's --video-fx. `Palette' picks one of
# four: 0 Ember, deep red to pale amber; 1 Ice, deep blue to near white;
# 2 Verdigris, sea green to pale green; 3 Violet, plum to pale pink.
# Between two, it blends them. `Mix' crossfades the picture's own colors
# to the palette's.
#
# Brightness is the usual 0.3 red, 0.59 green and 0.11 blue, held between
# 0 and 1; a pixel at b is 2b(1 - b) of the middle color plus b^2 of the
# light one, a curve the middle color bends rather than one it passes
# through, so black stays black and full white is the light color.

name "Palette";
author "Misha Nasledov";
description "The picture recolored by brightness into one of four palettes.";
category "Video";

    @palette = 0;
    @palette.widget = 1;
    @palette.min = 0;
    @palette.max = 3;
    @palette.step = 1;
    @palette.label = "Palette";

    @mix = 1;
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

node rin math::max { in0 = ionode->in0; in1 = 0; };
node gin math::max { in0 = ionode->in1; in1 = 0; };
node bin math::max { in0 = ionode->in2; in1 = 0; };
node l math::clamp { in = 0.3 * rin->out + 0.59 * gin->out + 0.11 * bin->out;
                     lo = 0; hi = 1; };
node mid math::mul { in0 = 2 * l->out; in1 = 1 - l->out; };
node top math::mul { in0 = l->out; in1 = l->out; };

# How much of each palette: 1 at its own number, falling to 0 at the next.
# Past either end, the end's palette rather than none.
node p math::clamp { in = @palette; lo = 0; hi = 3; };
node d0 math::abs { in = p->out; };
node d1 math::abs { in = p->out - 1; };
node d2 math::abs { in = p->out - 2; };
node d3 math::abs { in = p->out - 3; };
node w0 math::clamp { in = 1 - d0->out; lo = 0; hi = 1; };
node w1 math::clamp { in = 1 - d1->out; lo = 0; hi = 1; };
node w2 math::clamp { in = 1 - d2->out; lo = 0; hi = 1; };
node w3 math::clamp { in = 1 - d3->out; lo = 0; hi = 1; };

# Each sum is Ember, Ice, Verdigris and Violet, in that order.
node r math::add {
    in0 = rin->out * (1 - @mix);
    in1 = @mix * (mid->out * (0.55 * w0->out + 0.02 * w1->out + 0.00 * w2->out + 0.30 * w3->out) +
                  top->out * (1.00 * w0->out + 0.75 * w1->out + 0.75 * w2->out + 1.00 * w3->out));
};
node g math::add {
    in0 = gin->out * (1 - @mix);
    in1 = @mix * (mid->out * (0.08 * w0->out + 0.20 * w1->out + 0.35 * w2->out + 0.04 * w3->out) +
                  top->out * (0.85 * w0->out + 0.95 * w1->out + 1.00 * w2->out + 0.70 * w3->out));
};
node b math::add {
    in0 = bin->out * (1 - @mix);
    in1 = @mix * (mid->out * (0.02 * w0->out + 0.50 * w1->out + 0.30 * w2->out + 0.45 * w3->out) +
                  top->out * (0.50 * w0->out + 1.00 * w1->out + 0.65 * w2->out + 0.95 * w3->out));
};

io ionode;
