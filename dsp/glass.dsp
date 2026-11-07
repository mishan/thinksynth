# Glass -- dxbell's pair, let ring for twenty seconds.
#
# The same two operators as dxbell.dsp, a modulator at an inharmonic
# ratio into a carrier, but held and let go rather than struck: the note
# sustains while the key is down and rings for `Release' after, twenty
# seconds by default, which is what a glass or a tine left alone does in
# a large room. Under a reverb it is the top of an ambient piece.
#
# THE INDEX FALLS OVER THE RING. `Ring Decay' takes the modulation index
# down over the first sixteen seconds of the note, held or let go, so a
# note starts as glass -- the inharmonic partials a ratio of 3.5 puts
# around the pitch -- and ends as the carrier alone, a sine. The tone
# changes the whole time the note is heard, not only in its first second.
#
# VELOCITY IS THE INDEX, as on a DX tine: a note struck harder has more
# metal in it, and one played softly is nearly a sine from the start.
# aux0 places a composed note left to right.

name "Glass";
author "Misha Nasledov";
description "An inharmonic FM pair with a twenty-second release, its index falling across the ring and set by velocity.";
category "Keys";

    @ratio = 3.5;
    @ratio.widget = 1;
    @ratio.min = 1;
    @ratio.max = 14;
    @ratio.label = "Ratio";

    @index = 3;
    @index.widget = 1;
    @index.min = 0;
    @index.max = 10;
    @index.label = "Index";

    @id = 16000 ms;
    @id.widget = 1;
    @id.min = 500 ms;
    @id.max = 40000 ms;
    @id.label = "Ring Decay";

    # What is left of the index once `Ring Decay' has run out.
    @is = 0.05;
    @is.widget = 1;
    @is.min = 0;
    @is.max = 1;
    @is.label = "Ring Floor";

    @a = 6 ms;
    @a.widget = 1;
    @a.min = 0;
    @a.max = 2000 ms;
    @a.label = "Attack";

    @r = 20000 ms;
    @r.widget = 1;
    @r.min = 200 ms;
    @r.max = 40000 ms;
    @r.label = "Release";

    @level = 0.35;
    @level.widget = 1;
    @level.min = 0;
    @level.max = 1;
    @level.label = "Level";

node ionode {
    channels = 2;
    out0 = pan->out0;
    out1 = pan->out1;
    play = env->play;
};

node freq misc::midi2freq {
    note = ionode->note;
};

node idx env::ad {
    a = 1 ms;
    d = @id;
};

node modop osc::fmop {
    freq = freq->out;
    ratio = @ratio;
};

node carrier osc::fmop {
    freq = freq->out;
    ratio = 1;
    mod = modop->out;
    index = (@is + (1 - @is) * idx->out) * @index *
            (0.15 + ionode->velocity * 0.85);
};

# Held, a little under the strike, and the long ring after.
node env env::adsr {
    a = @a;
    d = 3000 ms;
    s = 0.6;
    r = @r;
    p = 0.5 + ionode->velocity * 0.5;
    trigger = ionode->trigger;
};

node pan mixer::pan {
    in = carrier->out * env->out * @level;
    pan = ionode->aux0;
};

io ionode;
