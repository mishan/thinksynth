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

/*
 * patch.js -- what a channel sounds like, and who decides it.
 *
 * A piece may aim its own channels: an `instrument' block is a .dsp the
 * piece carries and the loader puts on a channel of its own choosing. A
 * piece may also just name a channel -- `sink { channel = 4; }' -- and
 * leave the aiming to the reader, which the format allows and which nine
 * of the seventeen shipped pieces do. The desktop has an answer for one
 * of those: gthPrefs::LoadDefaults puts four patches on the first four
 * channels at first run, and every channel fern and hands name is one of
 * those four. The page had none, so those pieces composed thousands of
 * notes at a peak of zero -- or sounded, by accident, through whatever
 * the last thing the page did had left on the channel.
 *
 * THE RULE. What a channel sounds like is decided by the piece and,
 * where the piece is silent on the matter, by the defaults below. Never
 * by what the page did before. The same piece sounds the same on any
 * device in any order of clicks.
 *
 * So the order is: the piece first, then the aiming. aim() is called
 * after every load, with the channels the worklet says the piece named
 * and put nothing on, and it is a function of the piece and of what the
 * person has chosen by hand -- of nothing else.
 *
 * WHAT A .patch MEANS IS NOT DECIDED HERE. This file fetches, and that is
 * all it does. The text goes to the module, which reads it with the same
 * code the application runs (src/PatchFile.h) and puts it on the channel in
 * the order the format requires (src/PatchApply.h).
 *
 * There used to be a parser here -- a second reading of docs/DSP_FORMAT.md,
 * written separately -- and it had drifted. An `effect' line parsed as a
 * chanarg whose value was not a number and was dropped on the floor, so a
 * patch with a channel effect on it sounded different in a browser and said
 * nothing about why; `side' went to the engine as a chanarg called `side'.
 * Both of those work now, and no code here does them: they arrived by
 * deletion.
 *
 * The .dsp a patch names is not fetched here either. The page already
 * fetches every shipped .dsp at Start, to hand to the worklet as the
 * instruments a piece may look up, and that is what a patch's `dsp' line is
 * resolved against.
 */

/* Where the patches are, and the list of them the menu is filled from --
   relative names, `leads/SuperRes.patch', which is what the desktop's
   thinkrc calls them too. */
const PATCH_DIR = 'patches';

/* And where the graphs are, since a channel can be aimed at one of those
   as readily as at a patch of somebody's over it. */
const DSP_DIR = 'dsp';

/* The first-run configuration is the module's (src/PatchSet.h), and so is
 * the rule for a channel above the last entry in it. There used to be an
 * array here with a comment saying it was gthPrefs.cpp's table "exactly",
 * which is the kind of promise nothing keeps.
 *
 * Asked once and kept: the aiming below happens inside a load and a load has
 * no time to wait for anything, and the answer cannot change while a page is
 * open. What the page still does is fetch what the names name.
 */
let defaults = null;

export async function defaultNames (synth)
{
    if (defaults === null)
        defaults = (await synth.patchDefaults()).names;

    return defaults;
}

/* Fetched once each. A piece reload aims the same channels again, and the
   four defaults are the same four every time. */
const texts = new Map();

export function patchText (name)
{
    if (!texts.has(name))
        texts.set(name,
                  fetch(`${PATCH_DIR}/${name}`)
                      .then((r) =>
                      {
                          if (!r.ok)
                              throw new Error(
                                  `${name}: ${r.status} ${r.statusText}`);

                          return r.text();
                      })
                      .catch((e) =>
                      {
                          /* Not kept: a load after a failed fetch fetches
                             again. A 403 on a file the server will not
                             serve is exactly the failure this whole
                             thing was first reported as. */
                          texts.delete(name);
                          throw e;
                      }));

    return texts.get(name);
}

/* The list of shipped patches, by relative name. */
export function index ()
{
    return fetch(`${PATCH_DIR}/index.json`).then((r) => r.json());
}

/* A .patch onto a channel.
 *
 * Fetch the file, hand the text over, and say what went on. Resolves to what
 * was put there, or throws with something a person can read, since every
 * caller has a status line and a log.
 *
 * The document comes back from the module rather than being read here: what
 * a patch calls itself is an `info title' line, and only something that has
 * read the file knows it.
 */
export async function load (synth, channel, name)
{
    const r = await synth.patch(channel, await patchText(name), name);

    if (!r.ok)
        throw new Error(`${name}: ${r.why}`);

    const doc = JSON.parse(r.json);

    /* What to call it in a row: the title the file gives itself, or its
       bare name without the drawer it lives in -- `SuperRes', which is
       what the menu offering it says too. */
    return { patch: name, dsp: doc.dsp, generation: doc.generation,
             title: doc.info.title ??
                    name.split('/').pop().replace(/\.patch$/, '') };
}

/* A name from a menu, onto a channel, whichever kind of name it is.
 *
 * Two things can be chosen as an instrument and they are not the same
 * thing. A .patch is a graph and a set of values -- somebody's Acid Bass,
 * which is ts1.dsp turned a particular way. A .dsp is the graph itself,
 * at whatever its own file says, which is where you start when you want
 * to make one of your own rather than play one of theirs.
 *
 * They live in different directories and load by different calls, and the
 * one thing that must not happen is a caller having to know which it is
 * holding: the menus offer both at different altitudes now, and every
 * path that puts a choice on a channel comes through here.
 *
 * Resolves to the same shape `load' does, so a row drawn from it reads
 * the same whichever was chosen. A graph has no patch name and no
 * generation of its own -- nothing was read onto the channel -- and its
 * title is its filename, which is what the graph menu calls it too.
 */
export async function place (synth, channel, name)
{
    if (!name.endsWith('.dsp'))
        return load(synth, channel, name);

    const text = await graphText(name);

    if (!await synth.load(text, channel))
        throw new Error(`${name}: it did not parse`);

    return { patch: '', dsp: name, generation: 0,
             title: name.split('/').pop().replace(/\.dsp$/, '') };
}

/* And the graphs, fetched and kept the same way the patches are. */
const graphs = new Map();

export function graphText (name)
{
    if (!graphs.has(name))
        graphs.set(name,
                   fetch(`${DSP_DIR}/${name}`)
                       .then((r) =>
                       {
                           if (!r.ok)
                               throw new Error(
                                   `${name}: ${r.status} ${r.statusText}`);

                           return r.text();
                       })
                       .catch((e) =>
                       {
                           graphs.delete(name);
                           throw e;
                       }));

    return graphs.get(name);
}

/* The aiming, after a piece has loaded.
 *
 * `channels' is what the worklet said: the channels this piece's sinks
 * name that it put no instrument of its own on. `chosen' is what the
 * person has aimed by hand in this session, as a Map of channel to patch
 * name -- their choice outlives a reload of the piece, and a default must
 * not walk over it.
 *
 * Channels the piece's own instruments took are not in `channels' and are
 * left alone; they are the piece's. Channels the piece does not name are
 * left alone too -- whatever is on them is harmless, since nothing is
 * aimed at them.
 *
 * Resolves to what went where, so a caller can draw it, and to the
 * failures, so a caller can say them.
 */
export async function aim (synth, channels, chosen = new Map())
{
    const placed = new Map();
    const failed = [];

    /* In order, and awaited one at a time: each is a .dsp parsed on the
       audio thread, and the page has already suspended the context around
       the load these belong to. */
    for (const channel of channels)
    {
        const name = chosen.get(channel) ??
                     (await synth.patchDefault(channel)).name;

        try
        {
            placed.set(channel, await place(synth, channel, name));
        }
        catch (e)
        {
            failed.push(`channel ${channel + 1}: ${e.message}`);
        }
    }

    return { placed, failed };
}
