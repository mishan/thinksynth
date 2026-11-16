# Frame Drum -- a big single head on a hoop, iron hung inside it.
#
# The drum of the Siberian shaman and the Ainu kaco: a hide stretched
# loose over a bent-wood frame as wide as an arm, held by its crossbars
# and beaten with a padded stick. Nothing encloses the back, so there is
# no shell to boom and no air to pull the modes toward a pitch: what
# rings is the membrane alone, filt::membrane's modes, and it rings low
# and briefly.
#
# THE HEAD IS SLACK. A tight head rings bright and long; a slack one
# drops a little in pitch as the stroke pushes it out and lets it go,
# and its high modes die almost at once. The membrane's `damp' is that,
# and the fall in pitch over the first 80 ms is the stroke's push,
# deeper the harder it is.
#
# THE STICK IS PADDED. A soft beater stays on the head for a few
# milliseconds and so rings the upper modes less than the lower. A
# hard stroke is a shorter, taller bump and so a brighter one, and the
# membrane's tilt follows velocity as well: soft is a thump, hard is a
# thump with an edge. The note tunes the head over two octaves around
# 87 Hz, at half the keyboard's rate, so a melody on it is a drum's.
#
# THE IRON. The invented part: pendants hung on the crossbars, which
# swing into each other after every stroke and go on clattering after
# the head is quiet. Each clatter is a sample of white noise taken to a
# high power, which is zero nearly always and now and then a spike, and
# the spikes ring two filt::bar pendants of different sizes. The
# positive spikes strike one and the negative the other, so the two are
# never struck together. A harder stroke lowers the power, which is more
# spikes, and raises the level: a light tap is a few clinks, a hard one
# a rattle.

name "Frame Drum";
author "Misha Nasledov";
description "A slack hide on a wide frame, beaten with a padded stick, with iron pendants that rattle after each stroke.";
category "Drums";

    @tune = 0;
    @tune.widget = 1;
    @tune.min = -12;
    @tune.max = 12;
    @tune.step = 1;
    @tune.label = "Tune (semitones)";

    @decay = 0.9;
    @decay.widget = 1;
    @decay.min = 0.2;
    @decay.max = 3;
    @decay.label = "Head Decay (s)";

    @bend = 2.5;
    @bend.widget = 1;
    @bend.min = 0;
    @bend.max = 6;
    @bend.label = "Head Bend (semitones)";

    @stick = 2.5 ms;
    @stick.widget = 1;
    @stick.min = 1ms;
    @stick.max = 6ms;
    @stick.label = "Stick Contact";

    @iron = 0.6;
    @iron.widget = 1;
    @iron.min = 0;
    @iron.max = 1.5;
    @iron.label = "Iron";

    @clatter = 700 ms;
    @clatter.widget = 1;
    @clatter.min = 100ms;
    @clatter.max = 2500ms;
    @clatter.label = "Iron Clatter";

node ionode {
    channels = 2;

    # A roll stacks strokes: each rings under the next few, and the
    # oldest is stolen first, so a fast roll keeps its newest strokes.
    poly = 8;

    out0 = head->out + (pa->out * 0.22 + pb->out * 0.13) * @iron;
    out1 = head->out + (pa->out * 0.13 + pb->out * 0.22) * @iron;

    # The voice lasts as long as the longer of the head and the iron,
    # and the iron only counts when there is iron.
    play = max(head->play, rattle->play * min(@iron * 1000, 1));
};

node pitch misc::midi2freq {
    note = 41 + clamp(ionode->note - 48, -24, 24) * 0.5 + @tune;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

# The head's push and fall in pitch.
node fall env::ad {
    a = 0;
    d = 80 ms;
};

# The stick's bump: a triangle twice `Stick Contact' wide at no velocity
# and 0.4 of that at full. Its height is velocity over its width, so its
# area, which is what rings the low modes, is the stroke's momentum.
node bump env::ad {
    a = @stick * (1 - 0.6 * ionode->velocity);
    d = @stick * (1 - 0.6 * ionode->velocity);
};

node head filt::membrane {
    # A little noise on the stick, so no two strokes ring the same.
    in = bump->out * ionode->velocity * 0.4 / (1 - 0.6 * ionode->velocity) *
         (0.027 + noise->out * 0.0054);
    freq = pitch->out * exp2(@bend * ionode->velocity * fall->out / 12);
    modes = 10;
    decay = @decay;
    damp = 0.35;
    tilt = -9 + 8 * ionode->velocity;
};

# The pendants swing a moment after the head has thrown them.
node rattle env::ad {
    a = 20 ms;
    d = @clatter;
    p = ionode->velocity * ionode->velocity;
};

node pa filt::bar {
    in = pow(max(noise->out, 0), 4000 - 2500 * ionode->velocity) * rattle->out;
    freq = 1180;
    modes = 6;
    decay = 0.12;
    damp = 0.3;
};

node pb filt::bar {
    in = pow(max(noise->out * -1, 0), 4000 - 2500 * ionode->velocity) * rattle->out;
    freq = 1730;
    modes = 6;
    decay = 0.09;
    damp = 0.3;
};

io ionode;
