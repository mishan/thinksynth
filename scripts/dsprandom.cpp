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
 * dsprandom -- a random video patch from the kernels dspcompile has, for
 * compiledcheck to hold compiled against its nodes.
 *
 *   dsprandom SEED > patch.dsp
 *
 * The same seed writes the same patch on every platform: the generator is
 * this file's, not the library's. Each patch reads the raster (misc::scan's
 * every output), the note, its velocity and aux, two knobs and the music's,
 * through nodes wired to what came before them and to expressions of all
 * of it, so that what a value depends on -- a column, a row, a frame, every
 * pixel, or nothing past the call -- is a mix no hand-written patch has.
 * Red and green between them draw every node. Blue is the envelope alone,
 * so a patch is never dark however its red and green come out.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

using std::string;

static uint64_t state;

/* splitmix64. */
static uint64_t
next (void)
{
    uint64_t z = (state += 0x9e3779b97f4a7c15ull);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static int
pick (int n)
{
    return (int)(next() % (uint64_t)n);
}

/* A number in [lo, hi], in hundredths, written exactly. */
static string
number (int lo, int hi)
{
    const int v = lo * 100 + pick((hi - lo) * 100 + 1);
    char buf[32];

    snprintf(buf, sizeof buf, "%s%d.%02d", v < 0 ? "-" : "", abs(v) / 100, abs(v) % 100);
    return buf;
}

static std::vector<string> sources = {
    "s->x", "s->y", "s->t", "s->hsync", "s->vsync", "s->frame", "s->width", "s->size",
    "f->out * 0.01", "env->out", "ionode->velocity", "ionode->aux0", "ionode->note * 0.02",
    "@k0", "@k1",
    "@in_level", "@in_bass",
};

/* The first of `sources' no knob reaches. */
static const int UNKNOBBED = 13;

/* Of those, the ones held to 0..1. */
static const char *const UNIT[] = { "s->x", "s->y", "s->hsync", "s->vsync", "env->out" };

static string
term (bool knobs)
{
    return pick(4) == 0 ? number(-2, 2)
                        : sources[pick(knobs ? (int)sources.size() : UNKNOBBED)];
}

static string
expr (int depth, bool knobs = true)
{
    static const char *const OPS[] = { " + ", " - ", " * " };

    if (depth == 0 || pick(3) == 0)
        return term(knobs);

    /* One draw a statement: the operands of + are evaluated in an order
       of the compiler's choosing. */
    const string a = expr(depth - 1, knobs);
    const char *op = OPS[pick(3)];
    const string b = expr(depth - 1, knobs);

    return "(" + a + op + b + ")";
}

/* A kernel: its plugin, each input with what it is given if not an
   expression, and its outputs. */
struct Kind
{
    const char *plugin;
    std::vector<std::pair<const char *, const char *> > ins;
    std::vector<const char *> outs;
};

/* A voice that goes non-finite is retired, so a divisor and a base are
   numbers, and a power and an exponent no more than a number times
   something held to 0..1.

   "" is an expression; "~" one no knob reaches, for an arg the engine
   glides when one does, which dspcompile refuses; "u" a number times
   something held to 0..1; "ms" a time; anything
   else a range lo:hi of numbers, or a list a|b|c to choose one from. */
static const Kind KINDS[] = {
    { "math::add", { { "in0", "" }, { "in1", "" } }, { "out" } },
    { "math::sub", { { "in0", "" }, { "in1", "" } }, { "out" } },
    { "math::mul", { { "in0", "" }, { "in1", "" } }, { "out" } },
    { "math::div", { { "in0", "" }, { "in1", "1:4" } }, { "out" } },
    { "math::max", { { "in0", "" }, { "in1", "" } }, { "out" } },
    { "math::min", { { "in0", "" }, { "in1", "" } }, { "out" } },
    { "math::abs", { { "in", "" } }, { "out" } },
    { "math::exp2", { { "in", "u" } }, { "out" } },
    { "math::pow", { { "base", "1:4" }, { "exp", "u" } }, { "out" } },
    { "math::clamp", { { "in", "" }, { "lo", "-1:0" }, { "hi", "0:1" } }, { "out" } },
    { "math::sin", { { "index", "" }, { "wavelength", "" }, { "amp", "" } }, { "out" } },
    { "dist::saturate", { { "in", "" }, { "factor", "" } }, { "out" } },
    { "misc::latch", { { "in", "" }, { "latch", "" } }, { "out" } },
    { "filt::svf", { { "in", "" }, { "cutoff", "~" }, { "res", "~" } },
      { "out_low", "out_band", "out_high" } },
    { "delay::allpass", { { "in", "" }, { "delay", "1|7|40" }, { "gain", "-1:1" } },
      { "out" } },
    { "delay::echo", { { "in", "" }, { "size", "64|300" }, { "delay", "1|20|60" },
                       { "feedback", "0:1" }, { "dry", "0:1" }, { "tone", "0:1" },
                       { "low", "0:1" }, { "drive", "0:4" }, { "boost", "0:1" } }, { "out" } },
    { "env::ad", { { "a", "ms" }, { "d", "ms" }, { "p", "0:2" }, { "reset", "" } },
      { "out", "play" } },
    { "osc::simple", { { "freq", "" }, { "amp", "" }, { "waveform", "0|1|2|3" },
                       { "fm", "" }, { "fmamt", "" }, { "reset", "" } }, { "out" } },
    { "osc::blep", { { "freq", "" }, { "waveform", "0|1|2" }, { "pw", "0:1" },
                     { "reset", "" } }, { "out" } },
    { "osc::fmop", { { "freq", "" }, { "ratio", "0:4" }, { "mod", "" }, { "index", "" },
                     { "feedback", "0:1" } }, { "out" } },
    { "mixer::pan", { { "in", "" }, { "pan", "~" } }, { "out0", "out1" } },
    { "mixer::mul", { { "in0", "~" }, { "in1", "~" } }, { "out" } },
};

static string
given (const char *how)
{
    const string h = how;

    if (h.empty())
        return expr(2);
    if (h == "~")
        return expr(2, false);
    if (h == "u")
    {
        const string by = number(-4, 4);

        return by + " * " + UNIT[pick((int)(sizeof UNIT / sizeof UNIT[0]))];
    }
    if (h == "ms")
        return std::to_string(1 + pick(200)) + " ms";

    std::vector<string> parts;
    size_t at = 0;

    for (size_t bar; (bar = h.find('|', at)) != string::npos; at = bar + 1)
        parts.push_back(h.substr(at, bar - at));
    parts.push_back(h.substr(at));
    if (parts.size() > 1)
        return parts[pick((int)parts.size())];

    const size_t colon = h.find(':');

    return number(atoi(h.substr(0, colon).c_str()), atoi(h.substr(colon + 1).c_str()));
}

int
main (int argc, char **argv)
{
    if (argc != 2 || !*argv[1] || strspn(argv[1], "0123456789") != strlen(argv[1]))
    {
        fprintf(stderr, "usage: dsprandom SEED > patch.dsp\n");
        return 2;
    }
    const unsigned long long seed = strtoull(argv[1], NULL, 10);

    state = seed;

    string body, firsts, rest;
    const int count = 4 + pick(9);

    for (int i = 0; i < count; i++)
    {
        const Kind &k = KINDS[pick((int)(sizeof KINDS / sizeof KINDS[0]))];
        const string name = "n" + std::to_string(i);

        body += "node " + name + " " + k.plugin + " {";
        /* An expression is left at the plugin's default now and then; a
           number never, since a default base or divisor can be 0. */
        for (const auto &in : k.ins)
            if ((in.second[0] != '\0' && in.second[0] != '~') || pick(5) != 0)
            {
                const string value = given(in.second);

                body += string(" ") + in.first + " = " + value + ";";
            }
        body += " };\n";
        for (size_t o = 0; o < k.outs.size(); o++)
        {
            const string out = name + "->" + k.outs[o];

            sources.push_back(out);
            (o == 0 ? firsts : rest) += " + " + out;
        }
    }

    /* Red every node's first output, so that every node is drawn; green
       the rest and anything. Each draw its own statement, as above. */
    const string red = firsts.substr(3);
    const string more = expr(1);
    const string green = more + rest;
    const string k0 = number(-2, 2);
    const string k1 = number(0, 4);
    const int a = 1 + pick(20);
    const int d = 1 + pick(100);
    const string sustain = number(0, 1);
    const int r = 1 + pick(300);

    printf("name \"Random %llu\";\n\n"
           "@k0 = %s;\n@k1 = %s;\n@in_level = 0;\n@in_bass = 0;\n\n"
           "node ionode { channels = 3; out0 = env->out * (%s); out1 = env->out * (%s);\n"
           "              out2 = env->out; play = env->play; };\n"
           "node s misc::scan { };\n"
           "node f misc::midi2freq { note = ionode->note; };\n"
           "node env env::adsr { a = %d ms; d = %d ms; s = %s; r = %d ms; p = 1;\n"
           "                     trigger = ionode->trigger; };\n"
           "%s\nio ionode;\n",
           seed, k0.c_str(), k1.c_str(), red.c_str(), green.c_str(), a, d,
           sustain.c_str(), r, body.c_str());
    return 0;
}
