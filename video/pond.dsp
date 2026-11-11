# Pond -- each note a drop into still water, at a place its pitch
# chooses: a few crests spread from it slowly, widening and fading, and
# where two notes' ripples cross, crest on crest is brighter and crest on
# trough cancels.
#
# The ripple goes on after the note ends, until it has faded, unless ten
# newer notes on the channel are sounding: then the oldest is let go. `Speed' is
# how fast it spreads. `@in_level_slow', the music's loudness, brightens it,
# scaled by `Response'.

name "Pond";
author "Misha Nasledov";
description "A ripple per note, dropped where its pitch chooses, spreading and crossing.";
category "Video";

    @in_level_slow = 0;

    @speed = 1;
    @speed.widget = 1;
    @speed.min = 0.3;
    @speed.max = 3;
    @speed.label = "Speed";

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
    play = life->play;
};

node s misc::scan { };

# No sustain and no release: a ripple runs its course whatever the key
# does, within the channel's polyphony.
node life env::ad { a = 0; d = 7000 ms; };

# When the drop fell: the time, held from the voice's first sample.
node first env::ad { a = 0; d = 1; };
node t0 misc::latch { in = s->t; latch = first->play; };

# Where it fell, with constellation's steps: no two notes of the piano
# land on each other, and a chord or a run scatters.
node cx math::sin { index = ionode->note * 0.862030; amp = 0.36; };
node cy math::sin { index = ionode->note * 0.263310 + 0.068533; amp = 0.34; };

# Across in units of the picture's height: 1.33 is its 4:3.
node dx math::mul { in0 = s->x - 0.5 - cx->out; in1 = 1.33; };
node dy math::sub { in0 = s->y - 0.5; in1 = cy->out; };
node d2 math::add { in0 = dx->out * dx->out; in1 = dy->out * dy->out; };

# How far the ripple has gone, and how far a pixel is from it: near the
# ring, once it has spread a little, (d^2 - R^2) / 2R is the distance
# less R, with no square root.
node radius math::mul { in0 = (s->t - t0->out) * @speed; in1 = 0.06; };
node off math::div { in0 = d2->out - radius->out * radius->out;
                     in1 = 2 * radius->out + 0.01; };

# A crest at the ring and a trough either side, about six pixels apart,
# in a packet that widens as it goes, its half-width w2's square root.
node wave math::sin { index = off->out + 0.0125; wavelength = 0.05; };
node w2 math::mul { in0 = 0.03 + 0.15 * radius->out; in1 = 0.03 + 0.15 * radius->out; };
node packet math::div { in0 = w2->out; in1 = w2->out + off->out * off->out; };

# Fainter as it spreads, and gone when the drop's time is up.
node loud math::clamp { in = @in_level_slow * @response; lo = 0; hi = 1; };
node lit math::mul {
    in0 = wave->out * packet->out * packet->out * life->out / (1 + radius->out * 6);
    in1 = 0.6 + loud->out;
};

node r math::mul { in0 = lit->out; in1 = 0.45; };
node g math::mul { in0 = lit->out; in1 = 0.8; };
node b math::mul { in0 = lit->out; in1 = 1; };

io ionode;
