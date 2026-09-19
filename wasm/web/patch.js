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
 * A .patch is not a graph: it is a `dsp' line, `info' lines and flat `name
 * value[,value]' overrides for that DSP's chanargs (docs/DSP_FORMAT.md).
 * load() does what gthPatchManager::parse does, in the same order -- the .dsp
 * onto the channel, then each override -- at the same level,
 * TH_DEFAULT_CHAN_AMP, which tw_load applies for us.
 *
 * The .dsp a patch names is not fetched here. The page already fetches
 * every shipped .dsp at Start, to hand to the worklet as the instruments
 * a piece may look up, and those texts are what a patch's `dsp' line is
 * resolved against.
 */

/* Where the patches are, and the list of them the menu is filled from --
   relative names, `leads/SuperRes.patch', which is what the desktop's
   thinkrc calls them too. */
const PATCH_DIR = 'patches';

/* The application's first-run configuration, gthPrefs.cpp's
   thinkDefaultChannels, exactly.
 *
 * A channel above the last of these takes the entry at `c mod 4', so a
 * piece naming channel 7 sounds rather than not. The application leaves
 * channels 4 to 15 empty and gets away with it: it has a thinkrc a person
 * can edit, and a Patch Selector open in front of them. A page has no
 * first-run file, so it should not have the same gap. */
export const DEFAULTS = [
    'leads/SuperRes.patch',
    'bass/FunkMachine.patch',
    'organs/Organ1.patch',
    'pads/SynString.patch',
];

export function defaultFor (channel)
{
    return DEFAULTS[((channel % DEFAULTS.length) + DEFAULTS.length) %
                    DEFAULTS.length];
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

/* A .patch, read.
 *
 * gthPatchManager::parse's rules: leading space trimmed, blank lines and
 * `#' skipped, a line split at its first space and one with no space
 * ignored. `info NAME rest of line' is metadata, with `\n' unescaped.
 * Everything else is a chanarg and its comma-separated values.
 *
 * The whole list of values is kept, where the desktop's parser reads only
 * the first. A thArg holds as many as it was given, the format writes
 * `value[,value]', and dropping the rest here would make a .patch mean
 * something different in the page than on the desktop for the one file
 * that ever used it.
 */
export function parse (text)
{
    const out = { dsp: null, info: {}, args: [] };

    for (const line of text.split('\n'))
    {
        const trimmed = line.replace(/^[ \t]+/, '').replace(/[\r]+$/, '');

        if (trimmed === '' || trimmed.startsWith('#'))
            continue;

        const at = trimmed.indexOf(' ');

        if (at < 0)
            continue;

        const key = trimmed.slice(0, at);
        const rest = trimmed.slice(at + 1).replace(/^[ \t]+/, '');

        if (rest === '')
            continue;

        if (key === 'info')
        {
            const space = rest.indexOf(' ');

            /* `info foo' names a property and gives it nothing. */
            if (space < 0)
                continue;

            out.info[rest.slice(0, space)] =
                rest.slice(space + 1).replaceAll('\\n', '\n');
            continue;
        }

        const values = rest.split(',').map(Number);

        if (key === 'dsp')
        {
            /* Not a number: the first field is a file name. */
            out.dsp = rest.split(',')[0];
            continue;
        }

        if (values.some((v) => !Number.isFinite(v)))
            continue;

        out.args.push({ name: key, values });
    }

    return out;
}

/* A .patch onto a channel: the .dsp it names, then its overrides.
 *
 * `dsps' is the shipped .dsp texts by name -- the ones the page fetched
 * at Start and handed the worklet. Resolves to what was put there, or
 * throws with something a person can read, since every caller has a
 * status line and a log.
 */
export async function load (synth, channel, name, dsps)
{
    const patch = parse(await patchText(name));

    if (patch.dsp === null)
        throw new Error(`${name} names no .dsp`);

    const text = dsps[patch.dsp];

    if (text === undefined)
        throw new Error(`${name} wants ${patch.dsp}, which this build ` +
                        'does not ship');

    if (!await synth.load(text, channel))
        throw new Error(`${patch.dsp} did not parse`);

    /* After the load and not before: loading a .dsp builds the channel's
       chanargs from the file's declarations, and an override set first
       would be thrown away with the tree it was set on. */
    for (const a of patch.args)
        synth.chanarg(channel, a.name, a.values);

    /* What to call it in a row: the title the file gives itself, or its
       bare name without the drawer it lives in -- `SuperRes', which is
       what the menu offering it says too. */
    return { patch: name, dsp: patch.dsp,
             title: patch.info.title ??
                    name.split('/').pop().replace(/\.patch$/, '') };
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
export async function aim (synth, channels, dsps, chosen = new Map())
{
    const placed = new Map();
    const failed = [];

    /* In order, and awaited one at a time: each is a .dsp parsed on the
       audio thread, and the page has already suspended the context around
       the load these belong to. */
    for (const channel of channels)
    {
        const name = chosen.get(channel) ?? defaultFor(channel);

        try
        {
            placed.set(channel, await load(synth, channel, name, dsps));
        }
        catch (e)
        {
            failed.push(`channel ${channel + 1}: ${e.message}`);
        }
    }

    return { placed, failed };
}
