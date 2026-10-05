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
 * account.js -- what the relay and the page agree on about accounts: the
 * API's address, how a name is cleaned up and compared, and how a key is
 * read back from however it was typed. No DOM and no Node: both import it.
 *
 * An account is a handle and a key; no email, password or real name. The
 * relay makes the key -- eight words -- and keeps only its hash, so the key
 * is the account, and logging in trades it for a session the page keeps.
 */

import { SKELETON } from './confusables.js';

/* Where the relay serves the account routes, beside its sockets. */
export const ACCOUNT_API = '/api/account';

/* Words in a key, and the longest key a request may carry: eight long
   words and their separators, with room to spare. */
export const KEY_WORDS_PER_KEY = 8;
const KEY_INPUT_MAX = 200;

/* A name -- a handle, or a guest's -- in characters (grapheme clusters),
   and in UTF-16 units, which is what bounds a run of long emoji. */
export const NAME_MAX = 32;
const NAME_UNITS_MAX = 64;
const NAME_INPUT_MAX = 200;

/* Stripped outright: control characters other than whitespace, format
   characters other than the zero-width joiner and non-joiner, private-use,
   unassigned and lone-surrogate code points, marks that only ever render
   invisibly, and the variation selectors, Mongolian ones included, which
   change how a character is drawn and nothing about which it is. */
const STRIPPED = new RegExp(
    '(?!\\s)\\p{Cc}|(?!\\u200C|\\u200D)\\p{Cf}|[\\p{Co}\\p{Cn}\\p{Cs}' +
    '\\u034F\\u17B4\\u17B5\\u180B-\\u180D\\u180F\\uFE00-\\uFE0F' +
    '\\u{E0100}-\\u{E01EF}]', 'gu');

/* A run of whitespace, or of characters that render blank without being
   whitespace to Unicode: the Hangul fillers, the Mongolian vowel separator
   and the braille blank. */
const BLANK_RUN = /[\s\u115F\u1160\u180E\u2800\u3164\uFFA0]+/gu;

const JOINER_RUN = /(?:\u200C|\u200D)+/gu;

/* What a joiner means something between: emoji, which it joins into one
   picture, and the scripts whose letters it shapes. Anywhere else -- in
   Latin, say -- it is an invisible character that makes two names look
   alike and differ. */
const JOINS = new RegExp(
    '[\\p{Extended_Pictographic}\\p{Emoji_Modifier}' +
    '\\p{Script_Extensions=Arabic}' +
    '\\p{Script_Extensions=Syriac}\\p{Script_Extensions=Devanagari}' +
    '\\p{Script_Extensions=Bengali}\\p{Script_Extensions=Gurmukhi}' +
    '\\p{Script_Extensions=Gujarati}\\p{Script_Extensions=Oriya}' +
    '\\p{Script_Extensions=Tamil}\\p{Script_Extensions=Telugu}' +
    '\\p{Script_Extensions=Kannada}\\p{Script_Extensions=Malayalam}' +
    '\\p{Script_Extensions=Sinhala}]', 'u');

/* A joiner run stays only as a single joiner between two characters it
   means something between. */
function keepJoiner (run, at, text)
{
    const before = Array.from(text.slice(0, at)).at(-1);
    const after = Array.from(text.slice(at + run.length, at + run.length + 2))
        .at(0);

    return run.length === 1 && before !== undefined && after !== undefined &&
           JOINS.test(before) && JOINS.test(after) ? run : '';
}

let graphemes;

/* The first `max' characters of `text' that fit in `units' UTF-16 units:
   grapheme clusters, or code points where Intl.Segmenter is missing. */
function truncate (text, max, units)
{
    graphemes ??= typeof Intl.Segmenter === 'function'
        ? new Intl.Segmenter(undefined, { granularity: 'grapheme' }) : null;

    const chars = graphemes ? Array.from(graphemes.segment(text),
                                         (s) => s.segment)
                            : Array.from(text);
    let out = '';

    for (const c of chars.slice(0, max))
    {
        if (out.length + c.length > units)
            break;

        out += c;
    }

    return out;
}

/* A name as people will see it, or null if nothing visible is left: no
   invisible or direction-changing characters, blanks as single spaces,
   composed, at most two stacked marks, trimmed and cut to NAME_MAX. What
   two names are told apart by is foldName's. */
export function normalizeName (raw)
{
    if (typeof raw !== 'string' || raw.length > NAME_INPUT_MAX)
        return null;

    const cleaned = raw.replace(STRIPPED, '')
                       .replace(BLANK_RUN, ' ')
                       .replace(JOINER_RUN, keepJoiner)
                       .replace(/ {2,}/g, ' ')
                       .normalize('NFC')
                       .replace(/(\p{M}{2})\p{M}+/gu, '$1')
                       .trim();
    const name = truncate(cleaned, NAME_MAX, NAME_UNITS_MAX).trim();

    return name === '' ? null : name;
}

/* The form two names clash in: what they look like, by Unicode's
 * confusables (confusables.js), with case and compatibility forms left out.
 *
 * Decomposed first (NFKD: full-width letters and ligatures to their plain
 * letters, accents apart from them), so `\u0451' and `\u00EB' are each an
 * `e' and the same mark. Then the skeleton, once for the characters as
 * they are -- a capital Greek `\u039D' passes for `N', where the small one
 * passes for `v' -- and again after case is gone: upper-casing first
 * catches what lower-casing alone misses (`\u00DF' and `SS'). The dot a
 * capital `\u0130' leaves on its `i' is dropped.
 *
 * What this returns is stored (accounts.mjs, handle_folded and
 * kept_handles), so a change to it ships with a migration that folds every
 * stored handle again. */
export function foldName (name)
{
    const skeleton = (s) => Array.from(s, (c) => SKELETON.get(c) ?? c)
        .join('');

    return skeleton(skeleton(name.normalize('NFKD')).toUpperCase()
                        .toLowerCase())
        .replace(/l\u0307/g, 'l');
}

/* A key as typed or pasted, in its one form: lowercase words joined by
   hyphens, whatever separated them. Null if it is not eight words of
   letters. */
export function normalizeKey (raw)
{
    if (typeof raw !== 'string' || raw.length > KEY_INPUT_MAX)
        return null;

    const words = raw.toLowerCase().split(/[\s-]+/).filter(Boolean);

    return words.length === KEY_WORDS_PER_KEY &&
           words.every((w) => /^[a-z]+$/.test(w)) ? words.join('-') : null;
}

/* A name as the room shows it: a guest's says so. `account' is the
   relay's word, and a relay from before accounts says nothing, which
   marks nobody. */
export function shownName ({ name, account })
{
    return account === false ? `${name} (guest)` : name;
}

/* The relay's HTTP origin, for its WebSocket URL: wss://host/ is
   https://host. Null for a URL that is not one. */
export function apiOriginOf (relayUrl)
{
    try
    {
        const url = new URL(relayUrl);

        url.protocol = { 'wss:': 'https:', 'ws:': 'http:' }[url.protocol] ??
                       url.protocol;

        return /^https?:$/.test(url.protocol) ? url.origin : null;
    }
    catch
    {
        return null;
    }
}

/* Whether `raw' is an origin and nothing else, as a browser sends one in
   Origin and writes one into a WebAuthn response. */
export function isOrigin (raw)
{
    return URL.canParse(raw) && new URL(raw).origin === raw;
}

/* Whether a page on `host' may use passkeys for `rpId': WebAuthn takes
   an RP ID that is the host or a domain it is under. */
export function onRpId (host, rpId)
{
    return host === rpId || host.endsWith(`.${rpId}`);
}
