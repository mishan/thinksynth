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

/* Cyrillic and Greek letters drawn like Latin ones -- as capitals, mostly,
   which is why `\u043D' is an `h' -- and the digits and pairs that pass for
   letters: a cut of Unicode's confusables (UTS #39) to the set a handle
   is faked with.
   Folded alike, `p\u0430ypal' clashes with `paypal' whatever the script of
   its `a', and `I', `l' and `1' are one letter. Mixed scripts are
   otherwise allowed, so a name can mix a script with Latin. */
const LOOKALIKE = Object.fromEntries((
    '\u0430a \u0432b \u0441c \u0501d \u0435e \u04BBh \u0456l \u0458j ' +
    '\u043Ak \u04CFl \u043Cm \u043Dh \u043Eo \u0440p \u051Bq \u0455s ' +
    '\u0442t \u0443y \u0445x \u051Dw ' +
    '\u03B1a \u03B2b \u03B5e \u03B6z \u03B7h \u03B9l \u03BAk \u03BCm ' +
    '\u03BDn \u03BFo \u03C1p \u03C4t \u03C5y \u03C7x \u03C9w ' +
    '0o 1l il').split(' ').map((pair) => [...pair]));

/* The form two names clash in. Compatibility forms (full-width letters,
   ligatures) fold to their plain letters and case does not count;
   upper-casing first catches what lower-casing alone misses (`\u00DF' and
   `SS'), the dot `\u0130' leaves on its `i' is dropped, and lookalikes
   from another script fold to the Latin letter they pass for. */
export function foldName (name)
{
    return name.normalize('NFKC').toUpperCase().toLowerCase()
               .replace(/i\u0307/g, 'i')
               .replace(/./gu, (c) => LOOKALIKE[c] ?? c)
               .replace(/rn/g, 'm').replace(/vv/g, 'w')
               .normalize('NFC');
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
