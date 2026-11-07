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

/* say -- words, a syllable to a note.
 *
 * The melody is whatever the chain made; this stage puts the words on it.
 * `words' is English, spelled as written, and each note that passes takes
 * the next syllable with it as phonemes (thcEvent's `say'), for a voice
 * built on osc::speak to sing. The words run out and start again, so a
 * verse over a looping figure needs saying once.
 *
 * SPELLING TO SOUND is the NRL rules (english.h), with a short list of the
 * words they get wrong. Where they are wrong anyway, brackets take ARPAbet
 * as written: "[R OW - B AA T]", with `-' between syllables. A word is
 * split into syllables at its vowels -- `computer' is three notes -- and a
 * `-' in a word is only a reading aid.
 *
 * `~' HOLDS A SYLLABLE OVER ANOTHER NOTE: the note it lands on sings the
 * last syllable's vowel and nothing else, which is a melisma. `_' is a
 * note that says nothing at all.
 *
 * A CHORD IS ONE SYLLABLE. Notes that arrive at the same instant -- what
 * xform::harmonize makes of one -- all take the syllable the first did,
 * so a choir says a word together rather than a word each. A strummed
 * chord's notes arrive apart and take a syllable each, so a sung chord
 * wants `spread = 0'.
 *
 * DETERMINISM. None of it is random: the words are a function of how many
 * notes have passed, and a rewind is a fresh stage.
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "thcomposer.h"
#include "thPhoneme.h"
#include "english.h"

enum { P_WORDS, P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "words", "English, a syllable to a note; [K AH M] is ARPAbet, "
          "~ holds the last vowel, _ is a silent note",
          THC_PARAM_STRING, 0, 0, 0, "", NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Put the next syllable of the words on every note.");

    return 0;
}

typedef std::vector<unsigned char> Syllable;

/* `phonemes', space-separated ARPAbet, split at its vowels: each vowel
   takes the consonants before it, and of a run of consonants between two
   vowels the first stays with the one before. */
static void syllabify (const std::string &phonemes,
                       std::vector<Syllable> &out)
{
    std::vector<unsigned char> codes;
    size_t at = 0;

    /* Whitespace between names, any case, and the CMU dictionary's stress
       digits (AH0, AW1) dropped. */
    while (at < phonemes.size())
    {
        std::string name;

        while (at < phonemes.size() && phonemes[at] != ' ' &&
               phonemes[at] != '\t' && phonemes[at] != '\n')
        {
            const char c = phonemes[at++];

            if (!(c >= '0' && c <= '9'))
                name += englishUpper(c);
        }

        const int c = thPhonemeCode(name.c_str());

        if (c > 0)
            codes.push_back((unsigned char)c);

        at++;
    }

    std::vector<size_t> vowels;

    for (size_t i = 0; i < codes.size(); i++)
        if (thPhonemeIsVowel(codes[i]))
            vowels.push_back(i);

    if (vowels.size() <= 1)
    {
        if (!codes.empty())
            out.push_back(codes);
        return;
    }

    size_t from = 0;

    for (size_t v = 0; v + 1 < vowels.size(); v++)
    {
        const size_t between = vowels[v + 1] - vowels[v] - 1;
        const size_t cut = vowels[v] + 1 + (between >= 2 ? 1 : 0);

        out.push_back(Syllable(codes.begin() + from, codes.begin() + cut));
        from = cut;
    }

    out.push_back(Syllable(codes.begin() + from, codes.end()));
}

/* The last vowel of `s', or 0. */
static unsigned char vowelOf (const Syllable &s)
{
    for (size_t i = s.size(); i-- > 0; )
        if (thPhonemeIsVowel(s[i]))
            return s[i];

    return 0;
}

static void parse (const char *text, std::vector<Syllable> &out)
{
    const std::string t = text ? text : "";
    size_t i = 0;

    out.clear();

    while (i < t.size())
    {
        const char c = t[i];

        if (c == '[')
        {
            size_t end = t.find(']', i);

            if (end == std::string::npos)
                end = t.size();

            /* `-' between syllables; without one, the run is a word and
               is split at its vowels like any other. */
            const std::string inside = t.substr(i + 1, end - i - 1);

            if (inside.find('-') == std::string::npos)
                syllabify(inside, out);
            else
            {
                size_t from = 0;

                while (from <= inside.size())
                {
                    size_t dash = inside.find('-', from);

                    if (dash == std::string::npos)
                        dash = inside.size();

                    std::vector<Syllable> one;

                    syllabify(inside.substr(from, dash - from) + " ", one);

                    Syllable joined;

                    for (const Syllable &s : one)
                        joined.insert(joined.end(), s.begin(), s.end());
                    if (!joined.empty())
                        out.push_back(joined);

                    from = dash + 1;
                }
            }

            i = end + 1;
        }
        else if (c == '~')
        {
            const unsigned char v = out.empty() ? 0 : vowelOf(out.back());

            out.push_back(v ? Syllable(1, v) : Syllable());
            i++;
        }
        else if (c == '_')
        {
            out.push_back(Syllable(1, (unsigned char)thPhonemeCode("_")));
            i++;
        }
        else if (englishLetter(c) || c == '\'')
        {
            size_t end = i;

            while (end < t.size() &&
                   (englishLetter(t[end]) || t[end] == '\'' ||
                    t[end] == '-'))
                end++;

            std::string word;

            for (size_t k = i; k < end; k++)
                if (t[k] != '-')
                    word += t[k];

            syllabify(englishToPhonemes(word) + " ", out);
            i = end;
        }
        else
            i++;
    }
}

struct State {
    const thcParams     *params;
    std::vector<Syllable> syllables;
    size_t               next;      /* the syllable the next note takes */
    double               lastAt;    /* when the last note arrived */
    bool                 any;       /* whether one has */
};

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->next = 0;
    st->lastAt = 0;
    st->any = false;

    parse(params->get_string(params->ctx, paramIndex[P_WORDS]),
          st->syllables);

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    State *st = static_cast<State *>(state);

    if (index != paramIndex[P_WORDS])
        return;

    /* New words start from their first syllable. */
    parse(st->params->get_string(st->params->ctx, index), st->syllables);
    st->next = 0;
    st->any = false;
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);

    if (ev->type != THC_EV_NOTE || st->syllables.empty())
    {
        out->emit(out->ctx, ev);
        return;
    }

    /* A note at the instant of the last is the same chord: the same
       syllable, so the next stays where it is. */
    const bool chord = st->any && ev->at - st->lastAt < 1e-6 &&
                       st->lastAt - ev->at < 1e-6;
    size_t take = st->next;

    if (chord)
        take = (st->next + st->syllables.size() - 1) % st->syllables.size();
    else
        st->next = (st->next + 1) % st->syllables.size();

    st->lastAt = ev->at;
    st->any = true;

    thcEvent copy = *ev;
    const Syllable &s = st->syllables[take];
    size_t k = 0;

    for (; k < s.size() && k < THC_NOTE_SAY - 1; k++)
        copy.u.note.say[k] = s[k];
    for (; k < THC_NOTE_SAY; k++)
        copy.u.note.say[k] = 0;

    out->emit(out->ctx, &copy);
}
