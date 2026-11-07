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
 * localecheck -- libthink reads and writes numbers the same under any
 * LC_NUMERIC.
 *
 * The application pins LC_NUMERIC to "C" in main(), because atof and "%f"
 * follow it: under de_DE, atof reads `0.5' as 0 and "%f" writes a half as
 * `0,5'. A host that links libthink into itself cannot do that -- an audio
 * plugin inside a DAW shares a process with the DAW's own UI, which wants
 * the user's locale -- so the library's conversions go through
 * thUtil::parseDouble, parseFloat and formatFixed instead.
 *
 * Everything below is done twice, in "C" and then in a locale whose decimal
 * separator is a comma, and the two have to agree exactly:
 *
 *   the lexer's numbers       tokens of a line of .dsp text
 *   an expression's text      thExprText, which writes the shortest decimal
 *                             that reads back as the same float
 *   every file named          each chanarg's values, min, max and step, and
 *                             a hash of a note rendered through the graph
 *
 * The comma locale is proved to be in force before the second pass: atof
 * has to read `0.5' as 0 there, or the pass would show nothing.
 *
 * Then four threads convert at once, still under the comma locale, and
 * every result has to be right and the locale still a comma afterwards.
 * That is the host with several instances in it, and the host's own UI
 * thread reading the locale. A conversion that sets the process's locale
 * to "C" and back around itself -- which is what a stream imbued with the
 * classic locale does under MinGW's libstdc++ -- loses to that: one thread
 * puts the comma back while another is mid-parse.
 *
 *   scripts/localecheck -p build/plugins/ $(find dsp -name '*.dsp')
 *
 * Says SKIP and exits 77 when no comma locale is installed; the ctest
 * entry reads the SKIP. Otherwise the exit status is the number of
 * failures.
 */

#include "config.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "think.h"
#include "thExpr.h"
#include "thLexer.h"
#include "thUtil.h"

static int failed = 0;

static void fail (const string &what, const string &c, const string &comma)
{
    printf("FAIL  %s\n        C: %s\n    comma: %s\n", what.c_str(),
           c.c_str(), comma.c_str());
    failed++;
}

/* A float's bits, so that two values are equal here only if they are the
   same float. */
static string bits (float v)
{
    uint32_t u;
    char buf[16];

    memcpy(&u, &v, sizeof(u));
    snprintf(buf, sizeof(buf), "%08x", (unsigned)u);

    return buf;
}

static string lexed (const string &text)
{
    vector<thLexToken> tokens;
    string out;

    thLexString(text, tokens);

    for (size_t i = 0; i < tokens.size(); i++)
        if (tokens[i].kind == thLexToken::NUMBER)
            out += bits((float)tokens[i].num) + " ";

    return out;
}

static string printed (float v)
{
    thExprNode *e = thExprConst(v);
    const string text = thExprText(e);

    thExprFree(e);

    return text;
}

/* FNV-1a, 64-bit. */
static uint64_t hashBytes (uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    for (size_t i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }

    return h;
}

/* What loading and playing `file' comes to, as text: every chanarg, then
   the audio's hash. "" if it did not load. */
static string loaded (const string &pluginPath, const char *file)
{
    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    if (synth.loadTree(file, 0, 100) == NULL)
        return "";

    string out;
    const thArgMap args = synth.getChanArgs(0);

    for (thArgMap::const_iterator i = args.begin(); i != args.end(); ++i)
    {
        thArg *arg = i->second;

        out += i->first + "=";

        for (unsigned j = 0; j < arg->len(); j++)
            out += bits(arg->values()[j]) + ",";

        out += " [" + bits(arg->min()) + " " + bits(arg->max()) + " " +
               bits(arg->step()) + "] ";
    }

    synth.addNote(0, 60, 100);

    const size_t samples = thOutputSamples(synth.audioChannelCount(),
                                           synth.getWindowlen());
    uint64_t h = 0xcbf29ce484222325ULL;

    for (int w = 0; w < 4; w++)
    {
        synth.process();
        h = hashBytes(h, synth.getOutput(), samples * sizeof(float));
    }

    char buf[32];

    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);

    return out + buf;
}

static const char *const lexLine =
    "@a = 0.5; @b = 12.25; @c = 3.; @d = 0.1; @e = 440.0001;";

static const float printValues[] = { 0.5f, 0.1f, 12.25f, 1234.5f, 1e-7f };

struct Pass
{
    string lex;
    vector<string> print;
    vector<string> files;
};

static Pass run (const string &pluginPath, int argc, char **argv, int first)
{
    Pass p;

    p.lex = lexed(lexLine);

    for (size_t i = 0; i < sizeof(printValues) / sizeof(printValues[0]); i++)
        p.print.push_back(printed(printValues[i]));

    for (int f = first; f < argc; f++)
        p.files.push_back(loaded(pluginPath, argv[f]));

    return p;
}

static void convertMany (std::atomic<unsigned> *wrong)
{
    for (int i = 0; i < 20000; i++)
    {
        if (thUtil::parseDouble("0.5") != 0.5 ||
            thUtil::parseFloat("12.25") != 12.25f ||
            thUtil::formatFixed(0.25, 2) != "0.25")
            wrong->fetch_add(1, std::memory_order_relaxed);
    }
}

/* Four threads converting at once. Called with the comma locale set. */
static void threaded (const char *name)
{
    std::atomic<unsigned> wrong(0);
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; i++)
        threads.push_back(std::thread(convertMany, &wrong));

    for (size_t i = 0; i < threads.size(); i++)
        threads[i].join();

    if (wrong.load() != 0)
    {
        printf("FAIL  %u of %d conversions wrong with four threads "
               "converting under %s\n", wrong.load(), 4 * 20000 * 3, name);
        failed++;
    }

    const char *point = localeconv()->decimal_point;

    if (point[0] != ',')
    {
        printf("FAIL  four threads converting left the decimal point '%s', "
               "not the ',' of %s\n", point, name);
        failed++;
    }
}

/* A locale whose decimal separator is a comma, set for LC_NUMERIC, or NULL
   if none is installed. */
static const char *commaLocale (void)
{
    static const char *const names[] = {
        "de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8",
        "fr_FR", "nl_NL.UTF-8", "nl_NL.utf8", "ru_RU.UTF-8", "ru_RU.utf8",
        "German_Germany.1252", "de-DE",
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (setlocale(LC_NUMERIC, names[i]) != NULL &&
            localeconv()->decimal_point[0] == ',')
            return names[i];

    setlocale(LC_NUMERIC, "C");

    return NULL;
}

int main (int argc, char **argv)
{
    string pluginPath;
    int first = argc;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-p"))
        {
            if (++i >= argc)
                return 2;
            pluginPath = argv[i];
        }
        else
        {
            first = i;
            break;
        }
    }

    if (!pluginPath.empty() && pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    setlocale(LC_NUMERIC, "C");

    const Pass c = run(pluginPath, argc, argv, first);

    const char *name = commaLocale();

    if (name == NULL)
    {
        printf("SKIP  no locale with a comma decimal separator is installed\n");
        return 77;
    }

    /* The bug this is here for, present: without it the second pass would
       agree with the first for the wrong reason. */
    if (atof("0.5") != 0.0)
    {
        printf("FAIL  %s is set but atof still reads 0.5 as %g\n", name,
               atof("0.5"));
        return 1;
    }

    printf("LC_NUMERIC=%s, where atof reads 0.5 as 0\n", name);

    const Pass comma = run(pluginPath, argc, argv, first);

    threaded(name);

    setlocale(LC_NUMERIC, "C");

    if (comma.lex != c.lex)
        fail(string("lexing \"") + lexLine + "\"", c.lex, comma.lex);

    for (size_t i = 0; i < c.print.size(); i++)
        if (comma.print[i] != c.print[i])
            fail("thExprText of " + c.print[i], c.print[i], comma.print[i]);

    int files = 0;

    for (size_t i = 0; i < c.files.size(); i++)
    {
        const char *file = argv[first + (int)i];

        if (c.files[i].empty())
        {
            printf("FAIL  %s does not load\n", file);
            failed++;
        }
        else if (comma.files[i] != c.files[i])
            fail(file, c.files[i], comma.files[i]);
        else
            files++;
    }

    printf("%d file(s) identical under %s, %d failure(s)\n", files, name,
           failed);

    return failed;
}
