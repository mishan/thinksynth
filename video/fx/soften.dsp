# Soften -- the picture blurred, mixed back in, with a glow of the blur
# laid over it: hard edges go soft, bright things bloom.
#
# A video synth's effect, for genwav's --video-fx. The pixels come in
# raster order, so a blur can only look back: each of R, G and B is
# averaged 1-2-1 along the line from the last three pixels, then with the
# line above. The sharp picture is taken a pixel back too, so the two
# line up across, and the blur sits half a line above it, too little to
# see; the whole picture sits a pixel to the right.
#
# `Soften' crossfades the sharp picture to the blurred one; `Glow' adds
# the blur over both, brightening around whatever is lit. Fine moire
# blurs away as Soften goes up.
#
# What comes in is held at 0 and up first: a patch's negative half is
# black on screen, but blurred it would be a dark halo on its neighbors.

name "Soften";
author "Misha Nasledov";
description "A blur and a glow over the picture: hard edges soft, bright things blooming.";
category "Video";

    @soften = 0.6;
    @soften.widget = 1;
    @soften.min = 0;
    @soften.max = 1;
    @soften.label = "Soften";

    @glow = 0.4;
    @glow.widget = 1;
    @glow.min = 0;
    @glow.max = 2;
    @glow.label = "Glow";

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
node r1 delay::allpass { in = rin->out; delay = 1; gain = 0; };
node r2 delay::allpass { in = rin->out; delay = 2; gain = 0; };
node rh math::mul { in0 = rin->out + 2 * r1->out + r2->out; in1 = 0.25; };
node rv1 delay::allpass { in = rh->out; delay = s->width; gain = 0; };
node r math::add {
    in0 = r1->out * (1 - @soften);
    in1 = (rh->out + rv1->out) * 0.5 * (@soften + @glow);
};

node gin math::max { in0 = ionode->in1; in1 = 0; };
node g1 delay::allpass { in = gin->out; delay = 1; gain = 0; };
node g2 delay::allpass { in = gin->out; delay = 2; gain = 0; };
node gh math::mul { in0 = gin->out + 2 * g1->out + g2->out; in1 = 0.25; };
node gv1 delay::allpass { in = gh->out; delay = s->width; gain = 0; };
node g math::add {
    in0 = g1->out * (1 - @soften);
    in1 = (gh->out + gv1->out) * 0.5 * (@soften + @glow);
};

node bin math::max { in0 = ionode->in2; in1 = 0; };
node b1 delay::allpass { in = bin->out; delay = 1; gain = 0; };
node b2 delay::allpass { in = bin->out; delay = 2; gain = 0; };
node bh math::mul { in0 = bin->out + 2 * b1->out + b2->out; in1 = 0.25; };
node bv1 delay::allpass { in = bh->out; delay = s->width; gain = 0; };
node b math::add {
    in0 = b1->out * (1 - @soften);
    in1 = (bh->out + bv1->out) * 0.5 * (@soften + @glow);
};

io ionode;
