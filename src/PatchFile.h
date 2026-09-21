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

#ifndef PATCH_FILE_H
#define PATCH_FILE_H 1

/*
 * What a .patch *says*, with no file and no synth around it.
 *
 * The same move PanelModel made for a parameter panel, and CanvasContent for
 * a drawing. The format had two readings -- gthPatchManager::parse over
 * fgets() and strchr(), and a second parser in wasm/web/patch.js -- written
 * from the documentation separately, and they had already drifted: the page
 * dropped the `effect' line on the floor (it parses as a chanarg whose value
 * is not a number), sent `side' to the engine as a chanarg called `side', and
 * kept every value of a `name 1,2,3' line where the desktop kept only the
 * first. Two readings of one format, wrong in both directions.
 *
 * This is the reading, once: a string in, a document out, and the document
 * back to a string. What it does not have is a FILE *, a synth or a channel
 * -- so it compiles into the browser's module as readily as into the
 * application, which is what stops there being a second copy again. Finding
 * the file is the shell's business and stays there (gthPatchfile.cpp has
 * DSP_PATH and thinkrc; the page has fetch() and an index.json), and so is
 * putting the document on a channel, which is PatchApply.h.
 *
 * THE FORMAT (docs/DSP_FORMAT.md). Line-oriented, leading whitespace
 * ignored, `#' and blank lines skipped, every other line split at its first
 * space into a key and the rest:
 *
 *     dsp ts1.dsp            the graph this patch is; required
 *     side 4                 the channel the effect hears besides its own,
 *                            1-based here, engine numbering in the struct
 *     effect fx/delay.dsp    the graph on the channel's summed voices
 *     info title Super Res   metadata, `\n' escaped
 *     cutoff 1.04            a chanarg override, one value
 *     wave 1,2,3             or as many as the arg holds
 *
 * WHAT A BAD LINE DOES. It becomes a complaint and the rest of the file is
 * read. Only one thing fails a patch, and it is the one thing that makes it
 * not a patch: no `dsp' line. The desktop used to refuse the whole file over
 * an `info' line with no value -- a `goto owned' that shared its exit with a
 * DSP that would not load -- so a typo in a comment field cost the
 * instrument. That was never a decision; the page skipped the line and
 * carried on, and the page was right.
 *
 * WHAT IS NOT DECIDED HERE. Whether `side 99' names a channel, whether
 * anything declares `cutoff', whether the .dsp exists. All three need a synth
 * to answer and all three are PatchApply's. A document is what the file says,
 * and it round-trips: parse, compose, parse again, and the second document
 * equals the first. That property is what scripts/patchcheck holds the corpus
 * to and what lets a browser write a .patch the desktop will read back.
 */

#include <map>
#include <string>
#include <vector>

/* Named explicitly rather than relying on think.h's `using namespace std'
   having been pulled in first, for the reason NodeGraph.h gives: this header
   is meant to be includable from a translation unit that knows nothing about
   libthink. */
using std::map;
using std::string;
using std::vector;

/* A .patch, read.
 *
 * Plain data. Nothing here points at a channel or a file, so a document
 * outlives both -- which is what lets one be parsed in a test, composed back
 * and compared, and what lets the browser hold what a slot was given without
 * holding the slot. */
struct thPatchDoc
{
    /* The graph it names, by the name the file gave -- `ts1.dsp', not the
       path that resolves to. A patch saved afterwards carries the short name
       it came with, which is what keeps a config portable across an install
       that moves. Empty only in a document nothing was parsed into. */
    string dsp;

    /* The graph on the channel's summed voices, or empty. Named the way
       `dsp' is, for the same reason. */
    string effect;

    /* The channel that effect listens to besides this one, in engine
     * numbering, or -1 for none.
     *
     * The file counts from 1, because the number a person reads off the mixer
     * is the one they expect to find in a file; this counts from 0, because
     * that is what loadEffect takes. The conversion is the whole of what
     * happens here -- a `side' the file gives as 0 or less is -1, and
     * anything else is kept as written even if no such channel exists.
     * Whether 98 is a channel depends on how many the synth has, which is not
     * a question a document can answer, so the clamp is PatchApply's. Keeping
     * the number instead of dropping it is also what makes the round trip
     * exact: compose writes back the `side' line the file had. */
    int side;

    /* Metadata, by property name. `title' is what a menu row calls the patch;
       `author', `category', `comments' and `revised' are the others the
       corpus uses, and nothing refuses a property it has not heard of.
       Newlines arrive unescaped and go back out escaped. */
    map<string, string> info;

    /* The chanarg overrides, by name, every value in the order written.
     *
     * A vector rather than a float: a thArg has always held a list, the
     * format has always written `value[,value]', and the desktop's parser
     * kept the first and dropped the rest into a map nothing read. The page
     * kept all of them and said in a comment that it meant to. This is the
     * one place the two readings are resolved toward the page rather than the
     * desktop, and what makes that safe is scripts/dspcheck over the corpus:
     * no shipped .patch has a multi-value line, so no shipped patch changes
     * meaning.
     *
     * `dsp', `side' and `effect' are not in here. They are the three keys the
     * format spends on structure, and the desktop left each of them in the
     * arg map as a 0 -- strtof of a filename -- where nothing read them
     * either. */
    map<string, vector<float> > args;

    /* Lines that said nothing, in the order they were met, each naming its
     * line number and what was wrong with it.
     *
     * Not errors: the document is usable and the caller decides whether
     * anybody is told. The desktop prints them, the gate asserts them, and a
     * page can put them in a status line. What they are for is that a .patch
     * with a typo in it used to be either silently two-thirds loaded or
     * refused outright, with nothing in either case saying which line. */
    vector<string> complaints;

    thPatchDoc (void) : side(-1) { }
};

/* Reads `text' into `doc', which is cleared first.
 *
 * False only when the text names no `dsp', with `why' saying so; every other
 * malformed line is a complaint on the document. True does not promise the
 * graph exists or that anything declares the args -- see the header comment.
 */
bool thPatchParse (const string &text, thPatchDoc &doc, string &why);

/* And back: the bytes a Save writes.
 *
 * Byte-for-byte what gthPatchManager::savePatch has written since 2004, which
 * is not a nicety -- it is what makes `parse, compose, parse' a test of the
 * reading rather than of the writer's taste, and what lets the browser offer
 * a patch file the desktop opens.
 *
 * `stamp' is the date for the banner comment, the one thing in the output a
 * document cannot know: the application passes ctime()'s line, a page passes
 * a Date, and a test passes nothing and gets bytes that do not depend on when
 * it ran. */
string thPatchCompose (const thPatchDoc &doc, const string &stamp = "");

#endif /* PATCH_FILE_H */
