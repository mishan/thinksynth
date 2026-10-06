# Reverse -- every sound arriving backwards, swelling into its own attack.
#
# delay::reverse on each side: the channel cut into chunks `Chunk' long,
# each played back to front, so what came in as a hit and a decay comes
# out as a rise and a stop. `Room' puts delay::fdn's reverb in front of
# it first, and then it is the reverse reverb: the tail of a note heard
# before the note, rising out of nothing. At 0 it is a backwards echo of
# the channel itself.
#
# Each moment comes out twice, once from each head, about a `Chunk' after
# it went in and never more than two; set `Chunk' near the gap between the
# phrases it is meant to lead into.

name "Reverse";
author "Misha Nasledov";
description "Backwards chunks of the channel, through a reverb first: the reverse reverb's swell.";
category "Effects";

    @size = 600 ms;
    @size.widget = 1;
    @size.min = 50ms;
    @size.max = 2000ms;
    @size.label = "Chunk";

    @room = 0.7;
    @room.widget = 1;
    @room.min = 0;
    @room.max = 1;
    @room.label = "Room";

    @decay = 3;
    @decay.widget = 1;
    @decay.min = 0.3;
    @decay.max = 12;
    @decay.label = "Decay (s)";

    @mix = 0.5;
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

node room delay::fdn {
    in = (ionode->in0 + ionode->in1) * 0.5;
    size = 1.5;
    decay = @decay;
    damping = 6000;
};

node revl delay::reverse {
    in = ionode->in0 * (1 - @room) + room->out * @room;
    size = @size;
};

node revr delay::reverse {
    in = ionode->in1 * (1 - @room) + room->out2 * @room;
    size = @size;
};

node mixl mixer::fade {
    in0 = ionode->in0;
    in1 = revl->out;
    fade = @mix;
};

node mixr mixer::fade {
    in0 = ionode->in1;
    in1 = revr->out;
    fade = @mix;
};

io ionode;
