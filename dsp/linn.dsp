# Linn -- one channel, six drums, chosen by the note number.
#
# What a drum machine is, once there is a sampler: a rack of one-shots
# with a key each. Six `osc::sample' nodes, all fed the same trigger, each
# selecting a wav for its hit, and five of them turned off at any instant by
# which note arrived. One channel plays the whole kit, so a piece writes
# a kit part the way it writes a melody -- one chain, one scale of drum
# notes -- rather than six channels that have to be kept in step.
#
# THE NOTES ARE THE GENERAL MIDI DRUM MAP, which is not an arbitrary
# choice: it is what every sequencer, every drum editor and every
# exported MIDI file since 1991 has meant by these numbers.
#
#     ..35  B1 and down   bd10          a second kick, darker
#     36-37 C2            kit kick      the kick
#     38    D2            kit snare
#     39-41 D#2           clap
#     42-45 F#2           kit hat, closed
#     46..  A#2 and up    kit hat, open
#
# ZONES AND NOT SIX EXACT NOTES, which is a decision worth writing down
# because the other way round was written first. A kit that answered only
# its own six numbers and stayed silent for everything else read as a
# broken graph to every harness that plays a graph and asks whether a
# sound came out -- dspprobe plays a C major triad and found a patch that
# retired all three voices before the first window; the browser's
# check.mjs plays middle C and called it silent. Both were right to. A
# .dsp that is silent at the note you pressed is indistinguishable from
# one that does not work, and being unplayable outside a drum editor is
# a worse property than being approximate above A#2.
#
# So every note lands on something and the six written above land where
# a drum editor expects. Middle C is an open hat.
#
# HOW A NOTE PICKS A ZONE, since the expression grammar has no
# comparison. One clamp per boundary gives "is the note at least this":
# `clamp((note - 36) * 1000 + 1, 0, 1)' is 1 at 36 and above and 0 below
# it, the multiply being what makes the next note down a whole unit away
# rather than a thousandth so the clamp saturates and the edge is square.
# Each zone is then the difference of two of those, and since the
# differences telescope they sum to exactly 1 for any note at all --
# which is the proof that one drum always plays and no note can fall
# between two of them. A comparison operator would be one more thing in
# the language for one caller; this is two functions the language has.
#
# WHAT IT COSTS. All six nodes run on every window of every voice: the
# gate turns off the audio, not the work. That is six interpolated reads
# a sample instead of one, which is a real cost and a small one beside
# repeated file reads it is *not* doing -- each wav is read once per
# synth and shared (see plugins/osc/sampleslot.h). The alternative is a
# `file' that could be swept by a chanarg, which is a filename decided
# per sample, which is a different and much worse program.
#
# `root' IS `Tune' FOR EVERY DRUM and `freq' is fixed, which is the
# inverse of how a pitched sampler is wired and is right here: a drum
# has no pitch to track, so what the knob does is play the whole kit
# faster or slower together -- the speed control on the front of the
# machine. At 1 a wav comes out at exactly one frame a sample.
#
# The wavs are the tree's own drum graphs, rendered by
# scripts/makekit.sh. See its head for why a kit made out of this
# repository is the only kind it can ship.

name "Linn";
author "Misha Nasledov";
description "Six sampled drums on one channel, chosen by the note number.";
category "Drums";

    # A ratio, so 2 is twice the speed and an octave up, the way a
    # sampler's pitch control has always worked.
    @tune = 1;
    @tune.widget = 1;
    @tune.min = 0.25;
    @tune.max = 4;
    @tune.label = "Tune";

    # Velocity picks one of the soft, mid and hard renders. At zero the
    # selected layer stays at its quietest; at one it follows the stroke.
    @layers = 1;
    @layers.widget = 1;
    @layers.min = 0;
    @layers.max = 1;
    @layers.label = "Layers";

    @kick = 1;
    @kick.widget = 1;
    @kick.min = 0;
    @kick.max = 2;
    @kick.label = "Kick";

    @snare = 1;
    @snare.widget = 1;
    @snare.min = 0;
    @snare.max = 2;
    @snare.label = "Snare";

    @clap = 1;
    @clap.widget = 1;
    @clap.min = 0;
    @clap.max = 2;
    @clap.label = "Clap";

    @hat = 1;
    @hat.widget = 1;
    @hat.min = 0;
    @hat.max = 2;
    @hat.label = "Hats";

node ionode {
    channels = 2;

    # Six at once is the whole kit sounding together, which is a fill and
    # not a mistake.
    poly = 8;

    out0 = mix->out;
    out1 = mix->out;
    play = playing->out;
};

# The five boundaries. Each is 1 from its note upwards and 0 below it.
node a36 math::clamp { in = (ionode->note - 36) * 1000 + 1; lo = 0; hi = 1; };
node a38 math::clamp { in = (ionode->note - 38) * 1000 + 1; lo = 0; hi = 1; };
node a39 math::clamp { in = (ionode->note - 39) * 1000 + 1; lo = 0; hi = 1; };
node a42 math::clamp { in = (ionode->note - 42) * 1000 + 1; lo = 0; hi = 1; };
node a46 math::clamp { in = (ionode->note - 46) * 1000 + 1; lo = 0; hi = 1; };

# The rack. `root' takes the tuning and `freq' is the same number the
# kit was rendered at, so `Tune' at 1 is one frame a sample: middle C
# over middle C, and no interpolation happening at all.
node bd osc::sample {
    file = "bd10.wav";
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

node kick osc::sample {
    file = "kit_kick_soft.wav";
    file2 = "kit_kick_mid.wav";
    file3 = "kit_kick_hard.wav";
    select = ionode->velocity * @layers;
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

node snare osc::sample {
    file = "kit_snare_soft.wav";
    file2 = "kit_snare_mid.wav";
    file3 = "kit_snare_hard.wav";
    select = ionode->velocity * @layers;
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

node clap osc::sample {
    file = "clap.wav";
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

node hatc osc::sample {
    # The closed zone never reaches the hard, open-hat render.
    file = "kit_hat_soft.wav";
    file2 = "kit_hat_soft.wav";
    file3 = "kit_hat_mid.wav";
    select = ionode->velocity * @layers;
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

node hato osc::sample {
    # The open zone starts half-open and reaches the long hard render.
    file = "kit_hat_mid.wav";
    file2 = "kit_hat_hard.wav";
    file3 = "kit_hat_hard.wav";
    select = ionode->velocity * @layers;
    freq = 261.63;
    root = 261.63 * @tune;
    trigger = ionode->trigger;
};

# One drum, at the velocity it was played. The level knobs are a mixer:
# a kit whose clap is too loud is a kit, not a graph to edit.
node mix mixer::mul {
    in0 = bd->out * (1 - a36->out) * @kick * 0.8 +
          kick->out * (a36->out - a38->out) * @kick +
          snare->out * (a38->out - a39->out) * @snare +
          clap->out * (a39->out - a42->out) * @clap +
          hatc->out * (a42->out - a46->out) * @hat +
          hato->out * a46->out * @hat;
    in1 = ionode->velocity;
};

# The note lasts as long as the drum that is sounding, and no longer:
# every node's `play' is its own file's length, and the zones pick the
# one that matters. Clamped because `play' is a flag and the zones,
# being a partition, hand back exactly one of them -- the clamp is what
# makes that a guarantee rather than an argument.
node playing math::clamp {
    in = bd->play * (1 - a36->out) +
         kick->play * (a36->out - a38->out) +
         snare->play * (a38->out - a39->out) +
         clap->play * (a39->out - a42->out) +
         hatc->play * (a42->out - a46->out) +
         hato->play * a46->out;
    lo = 0;
    hi = 1;
};

io ionode;
