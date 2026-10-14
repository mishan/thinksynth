# Taiko -- a barrel drum played by more than one person.
#
# What makes a taiko sound like a taiko and not a big tom is three
# things, and each is a node here.
#
# THE HEAD IS SLACK AND HUGE. A tom's head is tuned tight and falls a
# fourth; a taiko's is a hide the width of a person and falls further,
# more slowly, from a stroke that drives it hard. `Head Bend' and
# `Head Sweep' are that fall, and the head carries its second mode --
# 1.59 times the first, which is what a round membrane rings at and
# what no harmonic oscillator will give -- that dies early, so the
# stroke has an edge and the tail is a pitch.
#
# THE BARREL. A shell cut from one trunk rings under the head long
# after the stroke, low and narrow: a band a tritone below the head,
# rung by the stick and given its own longer decay, which is the boom
# that carries across a field. An octave down would put a C2's boom at
# 33 Hz, which is a boom only on speakers that can make one.
#
# THE ENSEMBLE. Taiko is played in groups, and no two players strike
# together: a second drum, a few cents off and `Spread' later, sounds
# beside the first and lands on the other side. `Ensemble' is how much
# of it there is -- 0 is one player, and the room is a drum kit.
#
# The stick is a slap of noise whose brightness is the velocity, so a
# hard stroke cracks and a soft one only thumps.

name "Taiko";
author "Misha Nasledov";
description "A slack head falling over a barrel that booms, and a second player a breath behind: the ensemble drum.";
category "Drums";

    @tune = 0;
    @tune.widget = 1;
    @tune.min = -12;
    @tune.max = 12;
    @tune.step = 1;
    @tune.label = "Tune (semitones)";

    @bend = 9;
    @bend.widget = 1;
    @bend.min = 0;
    @bend.max = 24;
    @bend.label = "Head Bend (semitones)";

    @sweep = 90 ms;
    @sweep.widget = 1;
    @sweep.min = 10ms;
    @sweep.max = 400ms;
    @sweep.label = "Head Sweep";

    @decay = 1400 ms;
    @decay.widget = 1;
    @decay.min = 200ms;
    @decay.max = 5000ms;
    @decay.label = "Decay";

    @barrel = 0.8;
    @barrel.widget = 1;
    @barrel.min = 0;
    @barrel.max = 2;
    @barrel.label = "Barrel";

    @slap = 0.5;
    @slap.widget = 1;
    @slap.min = 0;
    @slap.max = 2;
    @slap.label = "Stick Slap";

    @ensemble = 0.6;
    @ensemble.widget = 1;
    @ensemble.min = 0;
    @ensemble.max = 1;
    @ensemble.label = "Ensemble";

    @spread = 14 ms;
    @spread.widget = 1;
    @spread.min = 1ms;
    @spread.max = 40ms;
    @spread.label = "Spread";

    @drive = 1.6;
    @drive.widget = 1;
    @drive.min = 1;
    @drive.max = 6;
    @drive.label = "Drive";

node ionode {
    channels = 2;

    # A stroke rings under the next few; a fast roll steals the oldest.
    poly = 6;

    out0 = left->out;
    out1 = right->out;

    # The barrel rings longest, so the voice lasts as long as it does --
    # when it is there to ring: at Barrel 0 the head is the whole drum.
    play = max(env->play, benv->play * min(@barrel * 1000, 1));
};

node pitch misc::midi2freq {
    note = ionode->note + @tune;
};

node penv env::ad {
    a = 0;
    d = @sweep;
};

node fall math::exp2 {
    in = @bend * penv->out / 12;
};

node head osc::simple {
    freq = pitch->out * fall->out;
    waveform = 0;
};

# The second mode, inharmonic, and gone a quarter of the way into the
# stroke: the edge a hard hit has.
node mode2 osc::simple {
    freq = pitch->out * fall->out * 1.59;
    waveform = 0;
};

# The other player: the same drum a few cents flat.
node head2 osc::simple {
    freq = pitch->out * fall->out * 0.991;
    waveform = 0;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node tip env::ad {
    a = 0;
    d = 4 ms;
};

# The slap, brighter the harder the stroke.
node hit filt::svf {
    in = noise->out * tip->out;
    cutoff = 700 + 2600 * ionode->velocity * ionode->velocity;
    res = 0.25;
};

node barrel filt::svf {
    in = hit->out_band * 0.8 + head->out * 0.05;
    cutoff = pitch->out * 0.7;
    res = 0.985;
};

node env env::ad {
    a = 1 ms;
    d = @decay;
    p = ionode->velocity;
};

node edge env::ad {
    a = 1 ms;
    d = @decay * 0.25;
    p = ionode->velocity * ionode->velocity;
};

node benv env::ad {
    a = 4 ms;
    d = @decay * 1.5;
    p = ionode->velocity;
};

node one dist::saturate {
    in = head->out * env->out * 0.8 +
         mode2->out * edge->out * 0.35 +
         barrel->out_band * benv->out * @barrel * 1.2 +
         hit->out_band * @slap * ionode->velocity;
    factor = @drive;
};

node two dist::saturate {
    in = head2->out * env->out * 0.8 +
         barrel->out_band * benv->out * @barrel * 0.8 +
         hit->out_band * @slap * ionode->velocity * 0.7;
    factor = @drive;
};

# The second player, `Spread' behind.
node late delay::echo {
    in = two->out;
    size = 60 ms;
    delay = @spread;
    feedback = 0;
    dry = 0;
};

node left math::add {
    in0 = one->out * 0.6;
    in1 = late->out * @ensemble * 0.15;
};

node right math::add {
    in0 = one->out * (1 - @ensemble * 0.7) * 0.6;
    in1 = late->out * @ensemble * 0.6;
};

io ionode;
