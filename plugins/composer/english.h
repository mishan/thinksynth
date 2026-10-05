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
 * English spelling to ARPAbet, by rule.
 *
 * The Naval Research Laboratory's letter-to-sound rules (Elovitz, Johnson,
 * McHugh and Shore, 1976), which every hobbyist speech synthesizer of the
 * eighties carried in some form. A rule is a run of letters, the context
 * either side of it that has to match, and what it sounds like; the rules
 * for a letter are tried in order and the first that fits is used, so the
 * specific cases come before the general one that ends each list.
 *
 * A context is letters, matched as written, and these:
 *
 *     ' '  the edge of the word        #  one or more vowels
 *     '.'  a voiced consonant          ^  one consonant
 *     '+'  E, I or Y                   :  zero or more consonants
 *     '%'  a suffix: ER E ES ED ING ELY (right context only)
 *     '@'  a consonant that makes U long: T S R D L Z N J, TH CH SH
 *     '&'  a sibilant: S C G Z X J, CH SH
 *
 * The rules are about ninety percent right on running text, and the
 * misses are mostly the common words with spellings of their own, so
 * those come from `exceptions' first. AX, NRL's schwa, is written AH: the
 * phoneme set has no separate unstressed vowel.
 */

#ifndef TH_ENGLISH_H
#define TH_ENGLISH_H 1

#include <cstring>
#include <string>

struct EnglishRule
{
    const char *left, *match, *right, *out;
};

/* Words the rules get wrong, and the ones a piece of this tree's is
   likely to sing. */
static const char *const englishExceptions[][2] = {
    { "A", "AH" },              { "AND", "AE N D" },
    { "ARE", "AA R" },          { "AROUND", "AH R AW N D" },
    { "AUTOBAHN", "AW T OW B AA N" },
    { "BETTER", "B EH T ER" },  { "BINARY", "B AY N ER IY" },
    { "BREAK", "B R EY K" },    { "BUY", "B AY" },
    { "CALCULATOR", "K AE L K Y AH L EY T ER" },
    { "CITY", "S IH T IY" },
    { "COMPUTER", "K AH M P Y UW T ER" },
    { "DAFT", "D AE F T" },     { "DIGITAL", "D IH JH IH T AH L" },
    { "DO", "D UW" },           { "ELECTRIC", "IH L EH K T R IH K" },
    { "EYE", "AY" },            { "EYES", "AY Z" },
    { "EUROPE", "Y UH R AH P" },
    { "EXPRESS", "IH K S P R EH S" },
    { "FASTER", "F AE S T ER" },
    { "FOUR", "F AO R" },       { "FROM", "F R AH M" },
    { "HARDER", "HH AA R D ER" },
    { "HAVE", "HH AE V" },      { "HEART", "HH AA R T" },
    { "HEARTS", "HH AA R T S" },
    { "I", "AY" },
    { "IS", "IH Z" },           { "LOVE", "L AH V" },
    { "MACHINE", "M AH SH IY N" },
    { "MACHINES", "M AH SH IY N Z" },
    { "MODEL", "M AA D AH L" }, { "MUSIC", "M Y UW Z IH K" },
    { "NUMBERS", "N AH M B ER Z" },
    { "OF", "AH V" },           { "OFF", "AO F" },
    { "OH", "OW" },             { "ONE", "W AH N" },
    { "PLAYED", "P L EY D" },
    { "POCKET", "P AA K IH T" },
    { "PUNK", "P AH NG K" },    { "RADIO", "R EY D IY OW" },
    { "ROBOT", "R OW B AA T" }, { "ROBOTS", "R OW B AA T S" },
    { "RIVER", "R IH V ER" },
    { "SAID", "S EH D" },       { "SEVEN", "S EH V AH N" },
    { "SHOWROOM", "SH OW R UW M" },
    { "STAYED", "S T EY D" },
    { "STRONGER", "S T R AO NG G ER" },
    { "THE", "DH AH" },         { "THREE", "TH R IY" },
    { "TO", "T UW" },           { "TRANS", "T R AE N Z" },
    { "TWO", "T UW" },          { "VERY", "V EH R IY" },
    { "WAS", "W AA Z" },
    { "WE", "W IY" },           { "WHAT", "W AH T" },
    { "WORLD", "W ER L D" },    { "YOU", "Y UW" },
    { "YOUR", "Y AO R" },       { "ZERO", "Z IY R OW" },
};

/* `""' is any context at all; `" "' is the edge of the word. */
#define ANY ""
#define EDGE " "

static const EnglishRule rulesA[] = {
    { ANY, "A", EDGE, "AH" },           { EDGE, "ARE", EDGE, "AA R" },
    { EDGE, "AR", "O", "AH R" },        { ANY, "AR", "#", "EH R" },
    { "^", "AS", "#", "EY S" },         { ANY, "A", "WA", "AH" },
    { ANY, "AW", ANY, "AO" },           { " :", "ANY", ANY, "EH N IY" },
    { ANY, "A", "^+#", "EY" },          { "#:", "ALLY", ANY, "AH L IY" },
    { EDGE, "AL", "#", "AH L" },        { ANY, "AGAIN", ANY, "AH G EH N" },
    { "#:", "AG", "E", "IH JH" },       { ANY, "A", "^+:#", "AE" },
    { " :", "A", "^+ ", "EY" },         { ANY, "A", "^%", "EY" },
    { EDGE, "ARR", ANY, "AH R" },       { ANY, "ARR", ANY, "AE R" },
    { " :", "AR", EDGE, "AA R" },       { ANY, "AR", EDGE, "ER" },
    { ANY, "AR", ANY, "AA R" },         { ANY, "AIR", ANY, "EH R" },
    { ANY, "AI", ANY, "EY" },           { ANY, "AY", ANY, "EY" },
    { ANY, "AU", ANY, "AO" },           { "#:", "AL", EDGE, "AH L" },
    { "#:", "ALS", EDGE, "AH L Z" },    { ANY, "ALK", ANY, "AO K" },
    { ANY, "AL", "^", "AO L" },         { " :", "ABLE", ANY, "EY B AH L" },
    { ANY, "ABLE", ANY, "AH B AH L" },  { ANY, "ANG", "+", "EY N JH" },
    { ANY, "A", ANY, "AE" },
};

static const EnglishRule rulesB[] = {
    { EDGE, "BE", "^#", "B IH" },       { ANY, "BEING", ANY, "B IY IH NG" },
    { EDGE, "BOTH", EDGE, "B OW TH" },  { EDGE, "BUS", "#", "B IH Z" },
    { ANY, "BUIL", ANY, "B IH L" },     { ANY, "B", ANY, "B" },
};

static const EnglishRule rulesC[] = {
    { EDGE, "CH", "^", "K" },           { "^E", "CH", ANY, "K" },
    { ANY, "CH", ANY, "CH" },           { " S", "CI", "#", "S AY" },
    { ANY, "CI", "A", "SH" },           { ANY, "CI", "O", "SH" },
    { ANY, "CI", "EN", "SH" },          { ANY, "C", "+", "S" },
    { ANY, "CK", ANY, "K" },            { ANY, "COM", "%", "K AH M" },
    { ANY, "C", ANY, "K" },
};

static const EnglishRule rulesD[] = {
    { "#:", "DED", EDGE, "D IH D" },    { ".E", "D", EDGE, "D" },
    { "#:^E", "D", EDGE, "T" },         { EDGE, "DE", "^#", "D IH" },
    { EDGE, "DO", EDGE, "D UW" },       { EDGE, "DOES", ANY, "D AH Z" },
    { EDGE, "DOING", ANY, "D UW IH NG" },
    { EDGE, "DOW", ANY, "D AW" },       { ANY, "DU", "A", "JH UW" },
    { ANY, "D", ANY, "D" },
};

static const EnglishRule rulesE[] = {
    { "#:", "E", EDGE, "" },            { "'^", "E", EDGE, "" },
    { " :", "E", EDGE, "IY" },          { "#", "ED", EDGE, "D" },
    { "#:", "E", "D ", "" },            { ANY, "EV", "ER", "EH V" },
    { ANY, "E", "^%", "IY" },           { ANY, "ERI", "#", "IY R IY" },
    { ANY, "ERI", ANY, "EH R IH" },     { "#:", "ER", "#", "ER" },
    { ANY, "ER", "#", "EH R" },         { ANY, "ER", ANY, "ER" },
    { EDGE, "EVEN", ANY, "IY V EH N" }, { "#:", "E", "W", "" },
    { "@", "EW", ANY, "UW" },           { ANY, "EW", ANY, "Y UW" },
    { ANY, "E", "O", "IY" },            { "#:&", "ES", EDGE, "IH Z" },
    { "#:", "E", "S ", "" },            { "#:", "ELY", EDGE, "L IY" },
    { "#:", "EMENT", ANY, "M EH N T" }, { ANY, "EFUL", ANY, "F UH L" },
    { ANY, "EE", ANY, "IY" },           { ANY, "EARN", ANY, "ER N" },
    { EDGE, "EAR", "^", "ER" },         { ANY, "EAD", ANY, "EH D" },
    { "#:", "EA", EDGE, "IY AH" },      { ANY, "EA", "SU", "EH" },
    { ANY, "EA", ANY, "IY" },           { ANY, "EIGH", ANY, "EY" },
    { ANY, "EI", ANY, "IY" },           { EDGE, "EYE", ANY, "AY" },
    { ANY, "EY", ANY, "IY" },           { ANY, "EU", ANY, "Y UW" },
    { ANY, "E", ANY, "EH" },
};

static const EnglishRule rulesF[] = {
    { ANY, "FUL", ANY, "F UH L" },      { ANY, "F", ANY, "F" },
};

static const EnglishRule rulesG[] = {
    { ANY, "GIV", ANY, "G IH V" },      { EDGE, "G", "I^", "G" },
    { ANY, "GE", "T", "G EH" },         { "SU", "GGES", ANY, "G JH EH S" },
    { ANY, "GG", ANY, "G" },            { " B#", "G", ANY, "G" },
    { ANY, "G", "+", "JH" },            { ANY, "GREAT", ANY, "G R EY T" },
    { "#", "GH", ANY, "" },             { ANY, "G", ANY, "G" },
};

static const EnglishRule rulesH[] = {
    { EDGE, "HAV", ANY, "HH AE V" },    { EDGE, "HERE", ANY, "HH IY R" },
    { EDGE, "HOUR", ANY, "AW ER" },     { ANY, "HOW", ANY, "HH AW" },
    { ANY, "H", "#", "HH" },            { ANY, "H", ANY, "" },
};

static const EnglishRule rulesI[] = {
    { EDGE, "IN", ANY, "IH N" },        { EDGE, "I", EDGE, "AY" },
    { ANY, "IN", "D", "AY N" },         { ANY, "IER", ANY, "IY ER" },
    { "#:R", "IED", ANY, "IY D" },      { ANY, "IED", EDGE, "AY D" },
    { ANY, "IEN", ANY, "IY EH N" },     { ANY, "IE", "T", "AY EH" },
    { " :", "I", "%", "AY" },           { ANY, "I", "%", "IY" },
    { ANY, "IE", ANY, "IY" },           { ANY, "I", "^+:#", "IH" },
    { ANY, "IR", "#", "AY R" },         { ANY, "IZ", "%", "AY Z" },
    { ANY, "IS", "%", "AY Z" },         { ANY, "I", "D%", "AY" },
    { "+^", "I", "^+", "IH" },          { ANY, "I", "T%", "AY" },
    { "#:^", "I", "^+", "IH" },         { ANY, "I", "^+", "AY" },
    { ANY, "IR", ANY, "ER" },           { ANY, "IGH", ANY, "AY" },
    { ANY, "ILD", ANY, "AY L D" },      { ANY, "IGN", EDGE, "AY N" },
    { ANY, "IGN", "^", "AY N" },        { ANY, "IGN", "%", "AY N" },
    { ANY, "IQUE", ANY, "IY K" },       { ANY, "I", ANY, "IH" },
};

static const EnglishRule rulesJ[] = {
    { ANY, "J", ANY, "JH" },
};

static const EnglishRule rulesK[] = {
    { EDGE, "K", "N", "" },             { ANY, "K", ANY, "K" },
};

static const EnglishRule rulesL[] = {
    { ANY, "LO", "C#", "L OW" },        { "L", "L", ANY, "" },
    { "#:^", "L", "%", "AH L" },        { ANY, "LEAD", ANY, "L IY D" },
    { ANY, "L", ANY, "L" },
};

static const EnglishRule rulesM[] = {
    { ANY, "MOV", ANY, "M UW V" },      { ANY, "M", ANY, "M" },
};

static const EnglishRule rulesN[] = {
    { "E", "NG", "+", "N JH" },         { ANY, "NG", "R", "NG G" },
    { ANY, "NG", "#", "NG G" },         { ANY, "NGL", "%", "NG G AH L" },
    { ANY, "NG", ANY, "NG" },           { ANY, "NK", ANY, "NG K" },
    { EDGE, "NOW", EDGE, "N AW" },      { ANY, "N", ANY, "N" },
};

static const EnglishRule rulesO[] = {
    { ANY, "OF", EDGE, "AH V" },        { ANY, "OROUGH", ANY, "ER OW" },
    { "#:", "OR", EDGE, "ER" },         { "#:", "ORS", EDGE, "ER Z" },
    { ANY, "OR", ANY, "AO R" },         { EDGE, "ONE", ANY, "W AH N" },
    { ANY, "OW", ANY, "OW" },           { EDGE, "OVER", ANY, "OW V ER" },
    { ANY, "OV", ANY, "AH V" },         { ANY, "O", "^%", "OW" },
    { ANY, "O", "^EN", "OW" },          { ANY, "O", "^I#", "OW" },
    { ANY, "OL", "D", "OW L" },         { ANY, "OUGHT", ANY, "AO T" },
    { ANY, "OUGH", ANY, "AH F" },       { EDGE, "OU", ANY, "AW" },
    { "H", "OU", "S#", "AW" },          { ANY, "OUS", ANY, "AH S" },
    { ANY, "OUR", ANY, "AO R" },        { ANY, "OULD", ANY, "UH D" },
    { "^", "OU", "^L", "AH" },          { ANY, "OUP", ANY, "UW P" },
    { ANY, "OU", ANY, "AW" },           { ANY, "OY", ANY, "OY" },
    { ANY, "OING", ANY, "OW IH NG" },   { ANY, "OI", ANY, "OY" },
    { ANY, "OOR", ANY, "AO R" },        { ANY, "OOK", ANY, "UH K" },
    { ANY, "OOD", ANY, "UH D" },        { ANY, "OO", ANY, "UW" },
    { ANY, "O", "E", "OW" },            { ANY, "O", EDGE, "OW" },
    { ANY, "OA", ANY, "OW" },           { EDGE, "ONLY", ANY, "OW N L IY" },
    { EDGE, "ONCE", ANY, "W AH N S" },  { ANY, "ON'T", ANY, "OW N T" },
    { "C", "O", "N", "AA" },            { ANY, "O", "NG", "AO" },
    { " :^", "O", "N", "AH" },          { "I", "ON", ANY, "AH N" },
    { "#:", "ON", EDGE, "AH N" },       { "#^", "ON", ANY, "AH N" },
    { ANY, "O", "ST ", "OW" },          { ANY, "OF", "^", "AO F" },
    { ANY, "OTHER", ANY, "AH DH ER" },  { ANY, "OSS", EDGE, "AO S" },
    { "#:^", "OM", ANY, "AH M" },       { ANY, "O", ANY, "AA" },
};

static const EnglishRule rulesP[] = {
    { ANY, "PH", ANY, "F" },            { ANY, "PEOP", ANY, "P IY P" },
    { ANY, "POW", ANY, "P AW" },        { ANY, "PUT", EDGE, "P UH T" },
    { ANY, "P", ANY, "P" },
};

static const EnglishRule rulesQ[] = {
    { ANY, "QUAR", ANY, "K W AO R" },   { ANY, "QU", ANY, "K W" },
    { ANY, "Q", ANY, "K" },
};

static const EnglishRule rulesR[] = {
    { EDGE, "RE", "^#", "R IY" },       { ANY, "R", ANY, "R" },
};

static const EnglishRule rulesS[] = {
    { ANY, "SH", ANY, "SH" },           { "#", "SION", ANY, "ZH AH N" },
    { ANY, "SOME", ANY, "S AH M" },     { "#", "SUR", "#", "ZH ER" },
    { ANY, "SUR", "#", "SH ER" },       { "#", "SU", "#", "ZH UW" },
    { "#", "SSU", "#", "SH UW" },       { "#", "SED", EDGE, "Z D" },
    { "#", "S", "#", "Z" },             { ANY, "SAID", ANY, "S EH D" },
    { "^", "SION", ANY, "SH AH N" },    { ANY, "S", "S", "" },
    { ".", "S", EDGE, "Z" },            { "#:.E", "S", EDGE, "Z" },
    { "#:^##", "S", EDGE, "Z" },        { "#:^#", "S", EDGE, "S" },
    { "U", "S", EDGE, "S" },            { " :#", "S", EDGE, "Z" },
    { EDGE, "SCH", ANY, "S K" },        { ANY, "S", "C+", "" },
    { "#", "SM", ANY, "Z M" },          { "#", "SN", "'", "Z AH N" },
    { ANY, "S", ANY, "S" },
};

static const EnglishRule rulesT[] = {
    { EDGE, "THE", EDGE, "DH AH" },     { ANY, "TO", EDGE, "T UW" },
    { ANY, "THAT", EDGE, "DH AE T" },   { EDGE, "THIS", EDGE, "DH IH S" },
    { EDGE, "THEY", ANY, "DH EY" },     { EDGE, "THERE", ANY, "DH EH R" },
    { ANY, "THER", ANY, "DH ER" },      { ANY, "THEIR", ANY, "DH EH R" },
    { EDGE, "THAN", EDGE, "DH AE N" },  { EDGE, "THEM", EDGE, "DH EH M" },
    { ANY, "THESE", EDGE, "DH IY Z" },  { EDGE, "THEN", ANY, "DH EH N" },
    { ANY, "THROUGH", ANY, "TH R UW" }, { ANY, "THOSE", ANY, "DH OW Z" },
    { ANY, "THOUGH", EDGE, "DH OW" },   { EDGE, "THUS", ANY, "DH AH S" },
    { ANY, "TH", ANY, "TH" },           { "#:", "TED", EDGE, "T IH D" },
    { "S", "TI", "#N", "CH" },          { ANY, "TI", "O", "SH" },
    { ANY, "TI", "A", "SH" },           { ANY, "TIEN", ANY, "SH AH N" },
    { ANY, "TUR", "#", "CH ER" },       { ANY, "TU", "A", "CH UW" },
    { EDGE, "TWO", ANY, "T UW" },       { ANY, "T", ANY, "T" },
};

static const EnglishRule rulesU[] = {
    { EDGE, "UN", "I", "Y UW N" },      { EDGE, "UN", ANY, "AH N" },
    { EDGE, "UPON", ANY, "AH P AO N" }, { "@", "UR", "#", "UH R" },
    { ANY, "UR", "#", "Y UH R" },       { ANY, "UR", ANY, "ER" },
    { ANY, "U", "^ ", "AH" },           { ANY, "U", "^^", "AH" },
    { ANY, "UY", ANY, "AY" },           { " G", "U", "#", "" },
    { "G", "U", "%", "" },              { "G", "U", "#", "W" },
    { "#N", "U", ANY, "Y UW" },         { "@", "U", ANY, "UW" },
    { ANY, "U", ANY, "Y UW" },
};

static const EnglishRule rulesV[] = {
    { ANY, "VIEW", ANY, "V Y UW" },     { ANY, "V", ANY, "V" },
};

static const EnglishRule rulesW[] = {
    { EDGE, "WERE", ANY, "W ER" },      { ANY, "WA", "S", "W AA" },
    { ANY, "WA", "T", "W AA" },         { ANY, "WHERE", ANY, "W EH R" },
    { ANY, "WHAT", ANY, "W AA T" },     { ANY, "WHOL", ANY, "HH OW L" },
    { ANY, "WHO", ANY, "HH UW" },       { ANY, "WH", ANY, "W" },
    { ANY, "WAR", ANY, "W AO R" },      { ANY, "WOR", "^", "W ER" },
    { ANY, "WR", ANY, "R" },            { ANY, "W", ANY, "W" },
};

static const EnglishRule rulesX[] = {
    { ANY, "X", ANY, "K S" },
};

static const EnglishRule rulesY[] = {
    { ANY, "YOUNG", ANY, "Y AH NG" },   { EDGE, "YOU", ANY, "Y UW" },
    { EDGE, "YES", ANY, "Y EH S" },     { EDGE, "Y", ANY, "Y" },
    { "#:^", "Y", EDGE, "IY" },         { "#:^", "Y", "I", "IY" },
    { " :", "Y", EDGE, "AY" },          { " :", "Y", "#", "AY" },
    { " :", "Y", "^+:#", "IH" },        { " :", "Y", "^#", "AY" },
    { ANY, "Y", ANY, "IH" },
};

static const EnglishRule rulesZ[] = {
    { ANY, "Z", ANY, "Z" },
};

#undef ANY
#undef EDGE

struct EnglishRules
{
    const EnglishRule *rules;
    size_t count;
};

#define TH_RULES(r) { r, sizeof(r) / sizeof(r[0]) }

static const EnglishRules englishRules[26] = {
    TH_RULES(rulesA), TH_RULES(rulesB), TH_RULES(rulesC), TH_RULES(rulesD),
    TH_RULES(rulesE), TH_RULES(rulesF), TH_RULES(rulesG), TH_RULES(rulesH),
    TH_RULES(rulesI), TH_RULES(rulesJ), TH_RULES(rulesK), TH_RULES(rulesL),
    TH_RULES(rulesM), TH_RULES(rulesN), TH_RULES(rulesO), TH_RULES(rulesP),
    TH_RULES(rulesQ), TH_RULES(rulesR), TH_RULES(rulesS), TH_RULES(rulesT),
    TH_RULES(rulesU), TH_RULES(rulesV), TH_RULES(rulesW), TH_RULES(rulesX),
    TH_RULES(rulesY), TH_RULES(rulesZ),
};

#undef TH_RULES

/* ASCII and nothing else: the C library's letters follow the locale, and
   a Turkish one upper-cases `i' to itself, which is no index into the
   rules. */
static inline bool englishLetter (char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline char englishUpper (char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static inline bool englishVowel (char c)
{
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

static inline bool englishConsonant (char c)
{
    return englishLetter(c) && !englishVowel(c);
}

static inline bool englishVoiced (char c)
{
    return strchr("BDVGJLMNRWZ", c) != NULL && c != '\0';
}

/* `w' is the word in capitals with a space at each end. Does the left
   context `ctx' end at w[at - 1]? Read right to left. */
static bool englishLeft (const std::string &w, int at, const char *ctx)
{
    int i = at - 1;

    for (int k = (int)strlen(ctx) - 1; k >= 0; k--)
    {
        const char c = ctx[k];

        if (i < 0)
            return false;

        switch (c)
        {
            case '#':
                if (!englishVowel(w[i]))
                    return false;
                while (i >= 0 && englishVowel(w[i]))
                    i--;
                break;
            case ':':
                while (i >= 0 && englishConsonant(w[i]))
                    i--;
                break;
            case '^':
                if (!englishConsonant(w[i--]))
                    return false;
                break;
            case '.':
                if (!englishVoiced(w[i--]))
                    return false;
                break;
            case '+':
                if (!strchr("EIY", w[i--]))
                    return false;
                break;
            case '&':
                if (i >= 1 && (w.compare(i - 1, 2, "CH") == 0 ||
                               w.compare(i - 1, 2, "SH") == 0))
                    i -= 2;
                else if (strchr("SCGZXJ", w[i]) && w[i])
                    i--;
                else
                    return false;
                break;
            case '@':
                if (i >= 1 && (w.compare(i - 1, 2, "TH") == 0 ||
                               w.compare(i - 1, 2, "CH") == 0 ||
                               w.compare(i - 1, 2, "SH") == 0))
                    i -= 2;
                else if (strchr("TSRDLZNJ", w[i]) && w[i])
                    i--;
                else
                    return false;
                break;
            case ' ':
                if (englishLetter(w[i--]))
                    return false;
                break;
            default:
                if (w[i--] != c)
                    return false;
                break;
        }
    }

    return true;
}

/* And the right context `ctx', starting at w[at]. */
static bool englishRight (const std::string &w, int at, const char *ctx)
{
    const int n = (int)w.size();
    int i = at;

    for (const char *p = ctx; *p; p++)
    {
        if (i >= n)
            return false;

        switch (*p)
        {
            case '#':
                if (!englishVowel(w[i]))
                    return false;
                while (i < n && englishVowel(w[i]))
                    i++;
                break;
            case ':':
                while (i < n && englishConsonant(w[i]))
                    i++;
                break;
            case '^':
                if (!englishConsonant(w[i++]))
                    return false;
                break;
            case '.':
                if (!englishVoiced(w[i++]))
                    return false;
                break;
            case '+':
                if (!strchr("EIY", w[i++]))
                    return false;
                break;
            case '%':
            {
                static const char *const suffixes[] = {
                    "ING", "ELY", "ER", "ES", "ED", "E",
                };
                bool found = false;

                for (const char *s : suffixes)
                    if (w.compare(i, strlen(s), s) == 0)
                    {
                        i += (int)strlen(s);
                        found = true;
                        break;
                    }

                if (!found)
                    return false;
                break;
            }
            case '&':
                if (w.compare(i, 2, "CH") == 0 || w.compare(i, 2, "SH") == 0)
                    i += 2;
                else if (strchr("SCGZXJ", w[i]) && w[i])
                    i++;
                else
                    return false;
                break;
            case '@':
                if (w.compare(i, 2, "TH") == 0 ||
                    w.compare(i, 2, "CH") == 0 || w.compare(i, 2, "SH") == 0)
                    i += 2;
                else if (strchr("TSRDLZNJ", w[i]) && w[i])
                    i++;
                else
                    return false;
                break;
            case ' ':
                if (englishLetter(w[i++]))
                    return false;
                break;
            default:
                if (w[i++] != *p)
                    return false;
                break;
        }
    }

    return true;
}

/* `word', letters and apostrophes, as space-separated ARPAbet. */
static std::string englishToPhonemes (const std::string &word)
{
    std::string w = " ";

    for (char c : word)
        if (englishLetter(c) || c == '\'')
            w += englishUpper(c);

    w += " ";

    const std::string bare = w.substr(1, w.size() - 2);

    for (const auto &e : englishExceptions)
        if (bare == e[0])
            return e[1];

    std::string out;
    int at = 1;

    while (at < (int)w.size() - 1)
    {
        const char c = w[at];

        if (!englishLetter(c))
        {
            at++;
            continue;
        }

        const EnglishRules &rs = englishRules[c - 'A'];
        bool matched = false;

        for (size_t r = 0; r < rs.count; r++)
        {
            const EnglishRule &rule = rs.rules[r];
            const int len = (int)strlen(rule.match);

            if (w.compare(at, len, rule.match) != 0 ||
                !englishLeft(w, at, rule.left) ||
                !englishRight(w, at + len, rule.right))
                continue;

            /* A doubled letter is one sound -- HAPPY has one P -- and a
               rule that spells each half would make it two syllables. */
            if (rule.out[0])
            {
                const std::string sound = rule.out;
                const size_t last = out.rfind(' ');
                const std::string tail =
                    last == std::string::npos ? out : out.substr(last + 1);

                if (tail != sound.substr(0, sound.find(' ')))
                    out += (out.empty() ? "" : " ") + sound;
                else if (sound.find(' ') != std::string::npos)
                    out += sound.substr(sound.find(' '));
            }

            at += len;
            matched = true;
            break;
        }

        if (!matched)
            at++;
    }

    return out;
}

#endif /* TH_ENGLISH_H */
