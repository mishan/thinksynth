/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 * The phonemes a note can carry, by number.
 *
 * ARPAbet, the CMU dictionary's set, plus `_' for a short pause between
 * words. A note's `say' (thcEvent) is a run of these codes ending in 0, and
 * a voice reads the same codes off `ionode->say'; the composer that spells
 * the words and the oscillator that speaks them agree through this table
 * and nothing else. A code is its index here plus one, so 0 is the end.
 *
 * Header-only, because it is shared by a composer module, an oscillator
 * plugin and the tapes, none of which link against each other.
 */

#ifndef TH_PHONEME_H
#define TH_PHONEME_H 1

#include <string.h>

static const char *const thPhonemeNames[] = {
    "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW",
    "OY", "UH", "UW",
    "B", "CH", "D", "DH", "F", "G", "HH", "JH", "K", "L", "M", "N", "NG",
    "P", "R", "S", "SH", "T", "TH", "V", "W", "Y", "Z", "ZH",
    "_",
};

#define TH_PHONEMES ((int)(sizeof(thPhonemeNames) / sizeof(thPhonemeNames[0])))

/* The vowels are the first fifteen: the syllable nuclei. */
#define TH_PHONEME_VOWELS 15

/* 1..TH_PHONEMES for a name in the table, upper case; 0 for anything else. */
static inline int thPhonemeCode (const char *name)
{
    for (int i = 0; i < TH_PHONEMES; i++)
        if (strcmp(thPhonemeNames[i], name) == 0)
            return i + 1;

    return 0;
}

/* The name for a code, or NULL for 0 and anything out of range. */
static inline const char *thPhonemeName (int code)
{
    return code >= 1 && code <= TH_PHONEMES ? thPhonemeNames[code - 1] : NULL;
}

static inline bool thPhonemeIsVowel (int code)
{
    return code >= 1 && code <= TH_PHONEME_VOWELS;
}

/* `say' as the tapes print it, names joined by dots ("K.AH.M"), into
   `out'; empty for a note that says nothing. Stops at the first 0, at `n'
   codes, or where `out' is full. */
static inline void thPhonemeSpell (const unsigned char *say, int n, char *out,
                                   size_t size)
{
    size_t at = 0;

    if (size == 0)
        return;

    out[0] = '\0';

    for (int i = 0; i < n && say[i]; i++)
    {
        const char *name = thPhonemeName(say[i]);
        const size_t len = name ? strlen(name) : 1;

        if (at + len + 2 > size)
            break;

        if (i > 0)
            out[at++] = '.';

        memcpy(out + at, name ? name : "?", len);
        at += len;
        out[at] = '\0';
    }
}

#endif /* TH_PHONEME_H */
