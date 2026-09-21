/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef PATCH_APPLY_H
#define PATCH_APPLY_H 1

/*
 * A thPatchDoc onto a channel.
 *
 * PatchFile.h is what a .patch *says*; this is what it *does*, and the two
 * are separate because only one of them needs a synth. What lives here is the
 * handful of rules that need to know something a file cannot: whether the
 * graph parses, whether anything declares `cutoff', whether 99 is a channel.
 *
 * THE ORDER IS THE FORMAT. Both old copies re-derived it and docs/DSP_FORMAT.md
 * calls it load-bearing:
 *
 *   the graph first, because loading one is what builds the chanargs an
 *   override would otherwise be set on and thrown away with the tree;
 *
 *   the side before the effect, because the effect is built when it is
 *   loaded and its side is part of building it;
 *
 *   the effect before its `fx.' values, because they have nowhere to land
 *   until it is on the channel.
 *
 * WHAT IS NOT HERE. Finding the .patch. The application searches PATCH_PATH
 * and opens a file; the page fetches one and hands the text over. That
 * asymmetry is correct and neither half of it crosses -- see the bottom of
 * PATCH_PLAN's list of what not to do, and gthPatchfile.cpp, which is what is
 * left of the desktop's shell once this and PatchFile have taken the rest.
 *
 * Finding the *graph* does cross, and is the one lookup that does. Both
 * shells have put their .dsp files where thUtil::findDataFile looks for years
 * -- the application under DSP_PATH, the page in the module's own MEMFS at
 * /dsp, written there by tw_instrument before the first piece loads -- because
 * there is no way to load a graph without naming a file the parser can open.
 * Nothing about an install layout moves; what is shared is a function libthink
 * already offers both of them.
 *
 * WHO CALLS IT. The application's patch manager, and the page. Never the
 * scheduler: thcScheduler takes an InstrumentLoader precisely so the composer
 * host never learns what a patch tab is, and the application's hook is what
 * reaches this. Same grain, one level down.
 */

#include <string>
#include <vector>

#include "PatchFile.h"

class thSynth;

/* What happened, which is more than yes or no.
 *
 * An effect that would not load is not a failed patch: the instrument is up
 * and playable and what is missing is a delay, so it is a complaint and `ok'
 * stays true. A graph that would not load is the other thing, and nothing on
 * the channel is touched. */
struct thPatchApplied
{
    bool ok;

    /* The refusal, or empty. Set only where `ok' is false. */
    string why;

    /* What is on the channel now: the effect's name as the file gave it, or
       empty, and the side it was built with in engine numbering, or -1. Not
       simply doc.effect and doc.side -- the effect may have failed and the
       side may have been clamped -- so this is what a caller records, and
       what a Save would write back. */
    string effect;
    int side;

    /* What was odd but survivable, each line ready to print: an `fx.' name
     * nothing declares, a `side' that is no channel, an effect that would not
     * load.
     *
     * The document arrives with its own complaints about lines that said
     * nothing, and those are not repeated here -- the two lists are about
     * different things and a caller that wants both prints both. */
    vector<string> complaints;

    thPatchApplied (void) : ok(false), side(-1) { }
};

/* Puts `doc' on `channel' of `synth', in the order above.
 *
 * The channel is replaced only if the graph loads. On failure nothing has
 * changed and whatever was playing still is -- which is the promise
 * thSynth::loadTree makes, kept rather than restated. */
thPatchApplied thPatchApply (thSynth *synth, int channel,
                             const thPatchDoc &doc);

/* The path a `dsp' or `effect' line names, for opening.
 *
 * Absolute names and names that resolve from the working directory are left
 * alone; anything else is looked for under `dsp/'. Hands back the name as
 * given when nothing matches, so an error message names what the patch
 * actually asked for rather than the last place that was tried. */
string thPatchResolveDsp (const string &name);

#endif /* PATCH_APPLY_H */
