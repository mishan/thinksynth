# Tambourine -- twenty jingles, none of them in tune with the others.
#
# A tambourine is not a drum with a rattle on it: what a hand hears is
# pairs of loose zils slapping together, and the head is almost beside
# the point. So there is no head here at all -- two sets of inharmonic
# partials at different decays, and a slap of noise on the front to say
# something struck something.
#
# TWO SETS, AND WHY. One `osc::multiwave' with `pitchadd' is already
# inharmonic: the offset breaks the ratios that a multiplied series
# cannot help keeping. But one set decays as one sound, and a real
# tambourine's jingles do not agree about anything, least of all how
# long to ring. The second set is a fifth of a tone away and twice as
# long, so what is left after a hundred milliseconds is a different
# chord from what arrived -- which is the shimmer, and the reason a
# tambourine on a record does not sound like a sample of itself.
#
# `Shake' is the second set's decay against the first. Down it is a hand
# stopping the jingles; up it is a tambourine left hanging on its stand.
#
# Velocity is the hand: on the noise most of all, then the near jingles,
# and least on the long set -- a light tap moves the zils that are
# already touching and a hard one throws all of them.

name "Tambourine";
author "Misha Nasledov";
description "Two sets of inharmonic jingles at different decays, with a slap on the front.";
category "Drums";

    @freq = 1700;
    @freq.widget = 1;
    @freq.min = 400;
    @freq.max = 5000;
    @freq.label = "Jingles (Hz)";

    @pmul = 1.31;
    @pmul.widget = 1;
    @pmul.min = 1;
    @pmul.max = 3;
    @pmul.label = "Partial Ratio";

    @spread = 480;
    @spread.widget = 1;
    @spread.min = 0;
    @spread.max = 2000;
    @spread.label = "Spread (Hz)";

    @offset = 1.19;
    @offset.widget = 1;
    @offset.min = 1;
    @offset.max = 2;
    @offset.label = "Second Set";

    @decay = 120 ms;
    @decay.widget = 1;
    @decay.min = 10ms;
    @decay.max = 1500ms;
    @decay.label = "Decay";

    @shake = 2.4;
    @shake.widget = 1;
    @shake.min = 0.2;
    @shake.max = 8;
    @shake.label = "Shake";

    @slap = 0.5;
    @slap.widget = 1;
    @slap.min = 0;
    @slap.max = 1.5;
    @slap.label = "Slap";

    @tone = 4200;
    @tone.widget = 1;
    @tone.min = 800;
    @tone.max = 14000;
    @tone.label = "Edge (Hz)";

node ionode {
    channels = 2;

    # A tambourine shaken in sixteenths is several of them sounding at
    # once; the hand does not stop the last one to start the next.
    poly = 4;

    out0 = out->out;
    out1 = out->out;
    play = env2->play;
};

node near osc::multiwave {
    waves = 7;
    freq = @freq;
    amp = 0.9;
    pitchmul = @pmul;
    pitchadd = @spread;
    ampmul = 0.84;
};

# A fifth of a tone up and its own spread, so the two sets share no
# partial anywhere: two tambourines would, and one does not.
node far osc::multiwave {
    waves = 7;
    freq = @freq * @offset;
    amp = 0.9;
    pitchmul = @pmul;
    pitchadd = @spread * 1.37;
    ampmul = 0.88;
};

node noise osc::noise {
    color = 0;
    amp = 1;
};

node slap env::ad {
    a = 0;
    d = 6 ms;
    p = ionode->velocity;
};

node env1 env::ad {
    a = 0.2 ms;
    d = @decay;
    p = ionode->velocity;
};

node env2 env::ad {
    a = 0.2 ms;
    d = @decay * @shake;
    p = ionode->velocity * 0.7 + 0.3;
};

node edge filt::svf {
    in = near->out * 0.35 * env1->out + far->out * 0.3 * env2->out +
         noise->out * slap->out * @slap;
    cutoff = @tone;
    res = 0.2;
};

node out math::mul {
    in0 = edge->out_band * 0.5 + edge->out_high * 0.9;
    in1 = ionode->velocity * 0.5 + 0.5;
};

io ionode;
