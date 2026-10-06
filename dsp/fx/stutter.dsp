# Stutter -- the channel caught on one moment while `Hold' is up.
#
# delay::stutter on each side. While `Hold' is up the last `Length' of the
# channel repeats, round and round; when it comes down the channel is back
# as it was. `Length' is in beats of the piece's tempo, through
# misc::tempo, so a sixteenth stays a sixteenth whatever the piece is at:
# 0.25 is a sixteenth, 0.5 an eighth, 1 a beat. A piece rides `Hold' with a
# chanarg sink on `fx.hold', a step at a time from gen::steps, which is the
# beat repeat at the end of a phrase or a vocal caught on one syllable.

name "Stutter";
author "Misha Nasledov";
description "A beat repeat: the last sixteenth, eighth or beat of a channel, again and again while held.";
category "Effects";

    @hold = 0;
    @hold.widget = 1;
    @hold.min = 0;
    @hold.max = 1;
    @hold.label = "Hold";

    @beats = 0.25;
    @beats.widget = 1;
    @beats.min = 0.0625;
    @beats.max = 4;
    @beats.label = "Length (beats)";

node ionode {
    channels = 2;

    in0 = 0;
    in1 = 0;

    out0 = stutl->out;
    out1 = stutr->out;
};

node tempo misc::tempo { };

node stutl delay::stutter {
    in = ionode->in0;
    hold = @hold;
    length = tempo->beat * @beats;
};

node stutr delay::stutter {
    in = ionode->in1;
    hold = @hold;
    length = tempo->beat * @beats;
};

io ionode;
