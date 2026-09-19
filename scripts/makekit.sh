#!/bin/sh
#
# Copyright (C) 2004-2026 Metaphonic Labs
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.

# makekit.sh -- fills dsp/samples/ by rendering the tree's own drums.
#
# osc::sample needs something to play out of the box, and the tree cannot
# ship a LinnDrum's ROM: those recordings belong to somebody. What it can
# ship is a kit it made itself -- each wav here is one hit of one of the
# graphs already in dsp/, rendered through genwav, so the kit regenerates
# from source, changes when the graph that makes it changes, and has no
# provenance question attached to it at all.
#
# It is also a demonstration of the thing worth demonstrating: a sampler
# playing a synthesizer is how half the records of the period were made.
# The LinnDrum's own sounds were recordings of drums; the DMX's were
# recordings of a drum machine. What matters is the playback, not where
# the bytes came from.
#
#   scripts/makekit.sh                 # with the defaults below
#   cmake --build build --target kit   # the same, after a build
#
# Overridable, in the order anybody actually needs them:
#
#   GENWAV    where genwav is           (build/scripts/genwav)
#   PLUGINS   where the .so files are   (build/plugins)
#   DSP       the graphs to render      (dsp)
#   OUT       where the wavs go         (dsp/samples)
#
# ADDING A DRUM is one line in the table below: a name for the file, the
# graph, the note to hit it at, the velocity, and how many seconds to
# render. The seconds matter -- a wav here is trimmed by rendering
# exactly as long as the drum sounds, since a one-shot's `play' output is
# the file's own length and a second of silence on the end of a kick is a
# second the note goes on for.
#
# The graph column may name SEVERAL, joined with `+': `stab+brass' plays
# both at the same note at the same instant and records the pair. That is
# what an orchestra hit is -- an orchestra playing one chord, recorded --
# and it is a thing a sampler can do that a graph cannot, because what
# comes out is one sound rather than two channels that a piece has to
# keep in step.

set -e

GENWAV=${GENWAV:-build/scripts/genwav}
PLUGINS=${PLUGINS:-build/plugins}
DSP=${DSP:-dsp}
OUT=${OUT:-dsp/samples}

if [ ! -x "$GENWAV" ]; then
    echo "makekit: $GENWAV is not there; build it first, or set GENWAV" >&2
    exit 1
fi

# file            graph          note  vel  seconds
#
# Two hats from one graph at two velocities, because hat0 reads velocity
# as the pedal rather than as loudness -- see dsp/hat0.dsp.
#
# The velocities sit below the top on purpose. A drum rendered at full
# scale reaches the master limiter and comes out with its transient bent;
# what a sample should hold is the graph's own signal, and whatever plays
# it can be as loud as it likes. The lengths are measured rather than
# guessed -- each is a little past where that drum stops, checked by
# looking at where the last audible frame is.
#
# bd10 is much quieter than the rest because bd10.dsp is: it clips at any
# velocity over about sixty, and genwav refuses a render it had to clip.
# A sample's own level is the least interesting thing about it -- the
# graph that plays it sets that -- so the number is chosen to be clean
# rather than to match the others.
kit="
kick909       kick909      C2   96  0.42
snare         snare        C3  110  0.22
clap          clap         C3  110  0.22
hat_closed    hat0         C4   40  0.32
hat_open      hat0         C4  110  0.85
bd10          bd10         C2   45  0.22
orchhit       stab+brass   C4   70  1.10
"

mkdir -p "$OUT"

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT INT TERM

# Redirected rather than piped. The `exit 1' below has always ended this
# script -- the loop's non-zero status is the pipeline's, and `set -e' acts
# on it -- but it ended it at one remove, through a subshell and a shell
# option, and this script has already lost a failure to exactly that shape
# once. A here-document keeps the loop in this shell, so the exit is the
# exit, the EXIT trap runs on the way out, and a line added to the body
# later can count what it rendered without the count vanishing at `done'.
while read -r name graph note vel secs; do
    [ -n "$name" ] || continue

    # One note, at the top of the render, and nothing after it. `hold' is
    # short because every graph here ends its own note -- a drum has
    # nothing to sustain -- and `step' is longer than the render so a
    # second hit never arrives.
    cat > "$scratch/one.gen" <<EOF
name "kit";
author "thinksynth";
description "one hit of $graph, for dsp/samples/";

seed 1;

scale one "$note";
EOF

    # One instrument and one chain per graph named, so `stab+brass' is
    # both of them hitting the same note together. `hold' is short
    # because a drum ends its own note; a graph that sustains needs a
    # longer one, which is why it is a variable and not a constant.
    hold=0.05
    case "$graph" in
        *+*) hold=0.9 ;;
    esac

    n=0

    for one in $(echo "$graph" | tr '+' ' '); do
        n=$((n + 1))

        cat >> "$scratch/one.gen" <<EOF

instrument drum$n {
    dsp "$one.dsp";
    amp = 127;
};

chain hit$n {
    stage src gen::lsystem {
        axiom = "F";
        depth = 0;
        notes = one;
        step = 600 s;
        hold = $hold s;
        vel = $vel;
    };
    sink { instrument = drum$n; };
};
EOF
    done

    # -m, because osc::sample is mono and sums a stereo file on the way
    # in anyway; every drum graph in the tree writes the same signal to
    # both sides, so this halves the file and loses nothing.
    #
    # The status is checked by hand rather than left to `set -e'.
    # genwav exits non-zero when it had to clip -- which is exactly the
    # thing that must not go unnoticed here, since a clipped sample is a
    # bent transient in every piece that ever plays it -- and `set -e'
    # inside a `while' in a pipeline kills the subshell without a word,
    # so the kit came out one drum short and said nothing.
    if THINK_DSP_PATH="$DSP" "$GENWAV" -p "$PLUGINS" -s "$secs" -q -m \
           -o "$OUT/$name.wav" "$scratch/one.gen"
    then
        echo "  $OUT/$name.wav  ($graph at $note, velocity $vel, ${secs}s)"
    else
        echo "makekit: $graph at velocity $vel would not render cleanly;" \
             "lower its velocity in the table" >&2
        exit 1
    fi
done <<KIT
$kit
KIT

echo "makekit: $OUT is the tree's own kit"
