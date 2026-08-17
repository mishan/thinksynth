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
 * lexcheck -- one lexical layer, and the two languages that read it.
 *
 * .dsp and .gen share a lexer now (libthink/thLexer.h) instead of .gen
 * carrying a hand-written copy of thinklex.ll's rules. The copy was
 * correct when it was written; the argument for retiring it was never
 * that it was wrong, it was that nothing would notice when it stopped
 * being right. This harness is what notices.
 *
 * Three things are held down here, and the corpus sweeps cannot hold any
 * of them:
 *
 * 1. The lexical layer itself -- kinds, values, line numbers, and byte
 *    spans. dspcheck proves .dsp files still parse and gencheck proves
 *    .gen files still load, but neither can say anything about a span,
 *    and spans are what thcGenEdit splices files by. A span that is off
 *    by one corrupts a file quietly and no other test would see it.
 *
 * 2. What the lexer refuses. A character the language cannot spell is
 *    now an ERROR token in the stream rather than something flex's
 *    default rule echoes to stdout and forgets.
 *
 * 3. That .gen's view is the same scan. thcGenLoader::tokenize is an
 *    adapter -- it fuses `-' onto a number and `@' onto a name, because
 *    .gen has no arithmetic and no `@' operator -- and the risk of an
 *    adapter is that it quietly becomes a second scanner. So the fused
 *    tokens are checked against the spans the shared lexer produced.
 */

#include "config.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "thLexer.h"

#include "thcGenFile.h"

static int failures = 0;

static void
check (bool ok, const std::string &what, const std::string &detail = "")
{
    printf("%-58s %s%s%s\n", what.c_str(), ok ? "ok" : "FAILED",
           detail.empty() ? "" : "  ", detail.c_str());

    if (!ok)
        failures++;
}

/* ---- 1. the lexical layer --------------------------------------------- */

/* Every token's span must cut the source back into the token as written.
 * This is the property thcGenEdit depends on and the one a rewrite of the
 * scanner is most likely to break, because nothing else reads spans and
 * so nothing else complains. */
static void
checkSpans (const std::string &label, const std::string &text)
{
    std::vector<thLexToken> toks;

    if (!thLexString(text, toks))
    {
        check(false, label + ": lexes", "unexpected error");
        return;
    }

    bool ok = true;
    std::string why;

    for (size_t i = 0; i < toks.size(); i++)
    {
        const thLexToken &t = toks[i];

        if (t.off > t.end || t.end > text.size())
        {
            ok = false;
            why = "span out of range";
            break;
        }

        /* Spans never overlap and never run backwards: the stream reads
           left to right through the file, which is what lets an editor
           splice one span without recomputing the others. */
        if (i && t.off < toks[i - 1].end)
        {
            ok = false;
            why = "span overlaps the token before it";
            break;
        }

        std::string cut = text.substr(t.off, t.end - t.off);

        if (t.kind == thLexToken::WORD || t.kind == thLexToken::NUMBER ||
            t.kind == thLexToken::PUNCT)
        {
            if (cut != t.text)
            {
                ok = false;
                why = "'" + cut + "' spanned but '" + t.text + "' reported";
                break;
            }
        }
        else if (t.kind == thLexToken::STRING)
        {
            /* The quotes are inside the span and outside the text --
               deliberately, so that replacing a string's span replaces
               the quoting too. */
            if (cut != "\"" + t.text + "\"")
            {
                ok = false;
                why = "string span " + cut + " does not quote " + t.text;
                break;
            }
        }
    }

    check(ok, label + ": spans cut the source back into tokens", why);
}

static void
checkLexicalLayer (void)
{
    /* CRLF on purpose. The spans are byte offsets into the file as it sits
       on disk, so a \r is a byte like any other and has to be accounted
       for -- which is also why thLexStream requires a binary-mode FILE*. */
    const std::string dsp =
        "# a comment nobody lexes\r\n"
        "node foo::bar {\n"
        "    a = 5 ms;\n"
        "    b = @gain;\n"
        "    c = th_max - 1;\n"
        "    d = one -> two;\n"
        "};\n"
        "name \"a patch\";\n";

    const std::string gen =
        "name \"a piece\";\n"
        "@density .min = 0;\n"
        "chain c {\n"
        "    stage s gen::eno_line { period = 19.4 s; shift = -3; };\n"
        "    sink { channel = 0; };\n"
        "};\n";

    checkSpans("dsp", dsp);
    checkSpans("gen", gen);

    std::vector<thLexToken> t;

    check(thLexString("node foo::bar", t), "a clean scan reports success");
    check(t.size() == 5, "node foo::bar is four tokens and an END",
          std::to_string(t.size()));

    if (t.size() == 5)
    {
        check(t[0].kind == thLexToken::WORD && t[0].text == "node",
              "keywords come back as ordinary words");
        check(t[2].kind == thLexToken::PUNCT && t[2].text == "::",
              ":: is one token, not two colons");
        check(t[4].kind == thLexToken::END, "the stream ends with END");
    }

    /* Line numbers survive comments and blank lines, because an error
       that names the wrong line is worse than one that names none. */
    thLexString("# one\n\n# three\nfour\n", t);
    check(t.size() == 2 && t[0].line == 4, "a token knows which line it is on",
          t.empty() ? "" : std::to_string(t[0].line));

    /* `th_max' and friends are numbers to .dsp and words to the lexer:
       the split that lets one scanner serve two vocabularies. */
    thLexString("th_max ms beats", t);
    check(t.size() == 4 &&
          t[0].kind == thLexToken::WORD && t[1].kind == thLexToken::WORD &&
          t[2].kind == thLexToken::WORD,
          "vocabulary belongs to the language, not the lexer");

    /* No arithmetic here: `-' is an operator and the number is separate.
       .dsp wants exactly this, and .gen puts them back together. */
    thLexString("-3", t);
    check(t.size() == 3 && t[0].kind == thLexToken::PUNCT && t[0].text == "-" &&
          t[1].kind == thLexToken::NUMBER && t[1].num == 3,
          "a sign is punctuation, not part of the number");

    thLexString("\"one\" \"two\"", t);
    check(t.size() == 3 && t[0].text == "one" && t[1].text == "two",
          "two strings on a line stay two strings");
}

/* ---- 2. what it refuses ------------------------------------------------ */

static void
expectLexError (const char *label, const std::string &text,
                const std::string &expect, int line)
{
    std::vector<thLexToken> toks;

    if (thLexString(text, toks))
    {
        check(false, std::string(label) + ": refused", "it was accepted");
        return;
    }

    const thLexToken *bad = thLexError(toks);

    check(bad != NULL, std::string(label) + ": the stream carries its error");

    if (!bad)
        return;

    check(bad->text.find(expect) != std::string::npos,
          std::string(label) + ": says what is wrong", bad->text);
    check(bad->line == line, std::string(label) + ": says where",
          std::to_string(bad->line));
    check(toks[toks.size() - 1].kind == thLexToken::END,
          std::string(label) + ": a failed stream still ends with END");
}

static void
checkRefusals (void)
{
    expectLexError("unterminated", "name \"no end\n", "unterminated string", 1);
    expectLexError("stray", "node foo;\na ~ b;\n", "stray character '~'", 2);

    /* A comment runs to the newline and no further, so a quote inside one
       cannot open a string that swallows the rest of the file. */
    std::vector<thLexToken> t;

    check(thLexString("# a \" in a comment\nnode\n", t) && t.size() == 2,
          "a quote inside a comment stays inside it");

    /* The contract is that the stream always ends with END, and that has to
       hold for a scan that never started as well as for one that hit a
       stray character on line nine. A caller walking the vector must never
       have to ask which kind of failure it is looking at. */
    check(!thLexStream(NULL, t) && t.size() == 2 &&
          t[0].kind == thLexToken::ERROR && t[1].kind == thLexToken::END,
          "a stream that cannot be read is still ERROR then END");

    /* A read that fails partway must not be mistaken for end of file: a
       truncated buffer parses as a syntax error on whichever line the read
       stopped at, and the author goes looking at a line that is fine. A
       directory opens on Linux and fails on the first fread, which is the
       cheapest real ferror available; where it does not open, there is
       nothing to prove and this says so rather than failing. */
    FILE *dir = fopen(".", "rb");

    if (dir == NULL)
        printf("%-58s %s\n", "a failed read is not an end of file",
               "skipped; this platform will not open a directory");
    else
    {
        const bool ok = thLexStream(dir, t);

        fclose(dir);

        check(!ok && t.size() == 2 && t[0].kind == thLexToken::ERROR &&
              t[1].kind == thLexToken::END,
              "a failed read is not an end of file",
              t.empty() ? "" : t[0].text);
    }
}

/* ---- 3. .gen reads the same scan --------------------------------------- */

/* thcGenLoader::tokenize fuses two token pairs the shared lexer keeps
 * apart. The fusion is only honest if the result still spans exactly what
 * the scanner saw -- so the .gen token's span is checked against the two
 * spans it was made of. */
static void
checkGenAdapter (void)
{
    const std::string text = "shift = -3; prob = @density;";

    std::vector<thLexToken>  raw;
    std::vector<thcGenToken> gen;
    std::string              err;
    int                      line = 0;

    thLexString(text, raw);

    check(thcGenLoader::tokenize(text, gen, err, line),
          "gen: the shared scan tokenizes", err);

    bool foundNum = false, foundKnob = false;

    for (size_t i = 0; i < gen.size(); i++)
    {
        if (gen[i].kind == thcGenToken::NUMBER && gen[i].num == -3)
        {
            foundNum = true;
            check(text.substr(gen[i].off, gen[i].end - gen[i].off) == "-3",
                  "gen: a folded sign is inside the number's span");
        }

        if (gen[i].kind == thcGenToken::KNOB)
        {
            foundKnob = true;
            check(gen[i].text == "density" &&
                  text.substr(gen[i].off, gen[i].end - gen[i].off) == "@density",
                  "gen: a knob is spelled without its @ and spanned with it");
        }
    }

    check(foundNum, "gen: -3 is one negative number");
    check(foundKnob, "gen: @density is one knob");

    /* Every .gen token starts where some shared token started: the
       adapter may fuse, but it may not invent a boundary of its own. */
    bool aligned = true;

    for (size_t i = 0; i + 1 < gen.size() && aligned; i++)
    {
        bool seen = false;

        for (size_t j = 0; j < raw.size(); j++)
            if (raw[j].off == gen[i].off && raw[j].line == gen[i].line)
                seen = true;

        aligned = seen;
    }

    check(aligned, "gen: every token begins where the shared lexer began one");

    /* The refusals .gen has that .dsp does not. `+' is an operator to one
       language and a stray character to the other, which is exactly the
       kind of difference that belongs above the lexer rather than in it. */
    check(!thcGenLoader::tokenize("a = 1 + 2;", gen, err, line) &&
          err.find("stray character '+'") != std::string::npos,
          "gen: arithmetic is a stray character", err);

    check(!thcGenLoader::tokenize("prob = @;", gen, err, line) &&
          err.find("no knob name") != std::string::npos,
          "gen: a bare @ names nothing", err);

    /* `- 5' was never a literal, and folding by span is what keeps it
       from becoming one. */
    check(!thcGenLoader::tokenize("shift = - 5;", gen, err, line) &&
          err.find("stray character '-'") != std::string::npos,
          "gen: a detached sign is still not a number", err);
}

/* ---- 4. still pure ----------------------------------------------------- */

/* thinklang was made reentrant so a background evaluator could parse
 * candidate instruments while the live synth plays (COMPOSITION_HANDOFF.md
 * §8 step 1, §9 tier 3). The lexer is the half of that with state in it --
 * a scanner, a buffer, a line count -- and it now has two callers instead
 * of one, so the property is worth a tripwire rather than an assurance.
 *
 * Sixteen threads lexing different text at once must each get what they
 * would have got alone. A scanner per call is what makes that true; a
 * static anything would make it false. */
static void
checkConcurrency (void)
{
    const size_t threads = 16;

    std::vector<std::string>              texts(threads);
    std::vector<std::vector<thLexToken> > alone(threads), together(threads);

    for (size_t i = 0; i < threads; i++)
    {
        /* Different lengths and different line counts on purpose: two
           scanners sharing state would most likely show it as a line
           number or an offset borrowed from the other one. */
        texts[i] = "# " + std::to_string(i) + "\n";

        for (size_t j = 0; j <= i; j++)
            texts[i] += "node n" + std::to_string(j) + " { a = " +
                        std::to_string(j) + " ms; };\n";

        thLexString(texts[i], alone[i]);
    }

    std::vector<std::thread> pool;

    for (size_t i = 0; i < threads; i++)
        pool.push_back(std::thread([&texts, &together, i]()
                                   { thLexString(texts[i], together[i]); }));

    for (size_t i = 0; i < threads; i++)
        pool[i].join();

    bool same = true;

    for (size_t i = 0; i < threads && same; i++)
    {
        if (alone[i].size() != together[i].size())
        {
            same = false;
            break;
        }

        for (size_t j = 0; j < alone[i].size(); j++)
            if (alone[i][j].kind != together[i][j].kind ||
                alone[i][j].text != together[i][j].text ||
                alone[i][j].line != together[i][j].line ||
                alone[i][j].off  != together[i][j].off  ||
                alone[i][j].end  != together[i][j].end)
            {
                same = false;
                break;
            }
    }

    check(same, "sixteen lexes at once are sixteen lexes alone");
}

int
main (void)
{
    checkLexicalLayer();
    printf("\n");
    checkRefusals();
    printf("\n");
    checkGenAdapter();
    printf("\n");
    checkConcurrency();

    printf("\n%s\n", failures ? "lexcheck FAILED" : "lexcheck ok");

    return failures ? 1 : 0;
}
