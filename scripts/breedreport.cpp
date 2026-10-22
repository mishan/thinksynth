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
 * breedreport -- how a flock bred from a graph does, generation by
 * generation (src/NodeBreed.h), for weighing one way of breeding against
 * another.
 *
 * For each seed, a flock is bred from `base.dsp' as gen::graft breeds it,
 * and each generation's champion is described as JSON on stdout: its
 * edits, how far it is from the target, how far from the champion before,
 * its level, and how long the generation took. With -o, each seed's
 * champions are written one after another as DIR/seed-N.wav, each
 * playing the phrase the breeder listens to, and each as
 * DIR/seed-N-gen-G.dsp.
 *
 *   breedreport -p build/plugins/ [-t target.dsp | -w target.wav]
 *               [-s 1,2,3] [-g GENERATIONS] [-o DIR]
 *               [--population N] [--elites N] [--crossover P]
 *               [--mutation P] [--depth N] [--kinds "splice modulate"]
 *               [--tune N] base.dsp
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "JsonOut.h"
#include "NodeBreed.h"

using std::string;
using std::vector;

static bool readText (const string &path, string &text)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::ostringstream s;

    s << in.rdbuf();
    text = s.str();

    return in.good() || in.eof();
}

/* 16-bit PCM as it is, not normalized: the levels are what is compared. */
static bool writeWav (const string &path, const vector<float> &x,
                      int channels, int rate)
{
    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return false;

    auto u32 = [f] (uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { fwrite(&v, 2, 1, f); };
    const uint32_t bytes = (uint32_t)x.size() * 2;

    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16((uint16_t)channels);
    u32((uint32_t)rate);
    u32((uint32_t)(rate * channels * 2));
    u16((uint16_t)(channels * 2));
    u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);

    for (float v : x)
        u16((uint16_t)(int16_t)lrintf(fmaxf(-1, fminf(1, v)) * 32767));

    return fclose(f) == 0;
}

static void jsonKey (string &out, const char *key)
{
    if (out.back() != '{')
        out += ',';

    jsonString(out, key);
    out += ':';
}

static void jsonStrings (string &out, const vector<string> &v)
{
    out += '[';

    for (size_t i = 0; i < v.size(); i++)
    {
        if (i)
            out += ',';

        jsonString(out, v[i]);
    }

    out += ']';
}

/* null for no value, which jsonNumber would write as 0. */
static void number (string &out, double v)
{
    char b[32];

    if (std::isfinite(v))
    {
        snprintf(b, sizeof b, "%.3f", v);
        out += b;
    }
    else
        out += "null";
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH, targetPath, wavPath, outDir, basePath;
    vector<unsigned int> seeds;
    unsigned int generations = 10;
    NodeBreed::Params params;

    for (int i = 1; i < argc; i++)
    {
        const string a = argv[i];
        const bool more = i + 1 < argc;

        if (a == "-p" && more)
            pluginPath = argv[++i];
        else if (a == "-t" && more)
            targetPath = argv[++i];
        else if (a == "-w" && more)
            wavPath = argv[++i];
        else if (a == "-o" && more)
            outDir = argv[++i];
        else if (a == "-g" && more)
            generations = (unsigned int)atoi(argv[++i]);
        else if (a == "-s" && more)
            for (char *s = strtok(argv[++i], ","); s; s = strtok(NULL, ","))
                seeds.push_back((unsigned int)strtoul(s, NULL, 10));
        else if (a == "--population" && more)
            params.population = (size_t)atoi(argv[++i]);
        else if (a == "--elites" && more)
            params.elites = (size_t)atoi(argv[++i]);
        else if (a == "--crossover" && more)
            params.crossover = atof(argv[++i]);
        else if (a == "--mutation" && more)
            params.mutation = atof(argv[++i]);
        else if (a == "--depth" && more)
            params.depth = (size_t)atoi(argv[++i]);
        else if (a == "--kinds" && more)
            params.kinds = argv[++i];
        else if (a == "--tune" && more)
            params.tune = (unsigned int)atoi(argv[++i]);
        else if (a[0] != '-' && basePath.empty())
            basePath = a;
        else
        {
            fprintf(stderr, "usage: %s -p PLUGINS [-t target.dsp | -w "
                    "target.wav] [-s 1,2,3] [-g GENERATIONS] [-o DIR] "
                    "[--population N] [--elites N] [--crossover P] "
                    "[--mutation P] [--depth N] [--kinds K] [--tune N] "
                    "base.dsp\n", argv[0]);
            return 2;
        }
    }

    string base, target;

    if (basePath.empty() || !readText(basePath, base) ||
        (!targetPath.empty() && !readText(targetPath, target)))
    {
        fprintf(stderr, "%s: cannot read %s\n", argv[0],
                basePath.empty() ? "a base graph" :
                !target.empty() || targetPath.empty() ? basePath.c_str()
                                                      : targetPath.c_str());
        return 2;
    }

    if (seeds.empty())
        seeds.push_back(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynth heard(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    const NodeBreed::Ear ear = NodeBreed::localEar(pluginPath, base, target,
                                                   wavPath);
    const bool aimed = (bool)ear.target;
    const int channels = heard.audioChannelCount();
    const int rate = (int)TH_DEFAULT_SAMPLES;

    /* What the breeder hears of a graph, on a synth of the report's own,
       its noise started over as the ear's is. */
    auto listen = [&] (const string &text, double &rms, double &peak,
                       vector<float> *played)
    {
        heard.setSeed(1);

        return NodeMutate::level(heard, text, rms, peak, played);
    };

    double baseRms = 0, basePeak = 0;
    vector<float> phrase;

    if (!listen(base, baseRms, basePeak, &phrase))
    {
        fprintf(stderr, "%s: %s makes no sound to measure\n", argv[0],
                basePath.c_str());
        return 1;
    }

    const double phraseSeconds = (double)phrase.size() / channels / rate;
    string out = "{";

    jsonKey(out, "phraseSeconds");
    number(out, phraseSeconds);
    jsonKey(out, "declared");
    out += "{";
    jsonKey(out, "distance");
    number(out, aimed ? ear.target(base) : NAN);
    jsonKey(out, "rms");
    number(out, baseRms);
    jsonKey(out, "peak");
    number(out, basePeak);
    out += "}";
    jsonKey(out, "runs");
    out += "[";

    for (size_t s = 0; s < seeds.size(); s++)
    {
        NodeBreed::Flock flock;
        string last = base, why;
        vector<float> audio = phrase;
        bool ok = true;

        out += s ? ",{" : "{";
        jsonKey(out, "seed");
        out += std::to_string(seeds[s]);
        jsonKey(out, "generations");
        out += "[";

        for (unsigned int g = 1; ok && g <= generations; g++)
        {
            string champion;
            size_t depth = 0;
            const auto t0 = std::chrono::steady_clock::now();

            ok = NodeBreed::generation(&synth, base, flock, params,
                                       seeds[s] * 1000003u + g, ear,
                                       champion, depth, why);

            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();

            if (!ok)
                break;

            /* The champion's edits, in words, from the member it is. */
            vector<string> edits;

            for (const NodeBreed::Member &m : flock.members)
            {
                string text = base, what;
                vector<string> said;

                for (const NodeMutate::Op &op : m.ops)
                    if (NodeMutate::applyTagged(&synth, text, op, what))
                        said.push_back(what);

                if (text == champion)
                {
                    edits = said;
                    break;
                }
            }

            double rms = 0, peak = 0;
            const size_t from = audio.size();

            listen(champion, rms, peak, &audio);

            out += g > 1 ? ",{" : "{";
            jsonKey(out, "generation");
            out += std::to_string(g);
            jsonKey(out, "start");
            number(out, (double)from / channels / rate);
            jsonKey(out, "end");
            number(out, (double)audio.size() / channels / rate);
            jsonKey(out, "changed");
            out += champion != last ? "true" : "false";
            jsonKey(out, "depth");
            out += std::to_string(depth);
            jsonKey(out, "edits");
            jsonStrings(out, edits);
            jsonKey(out, "distance");
            number(out, aimed ? ear.target(champion) : NAN);
            jsonKey(out, "apart");
            number(out, ear.apart(champion, last));
            jsonKey(out, "rms");
            number(out, rms);
            jsonKey(out, "peak");
            number(out, peak);
            jsonKey(out, "ms");
            number(out, ms);
            out += "}";

            if (!outDir.empty())
            {
                std::ofstream dsp(outDir + "/seed-" + std::to_string(seeds[s]) +
                                  "-gen-" + std::to_string(g) + ".dsp");

                dsp << champion;
            }

            last = champion;
        }

        out += "]";

        if (!ok)
        {
            jsonKey(out, "error");
            jsonString(out, why);
        }

        out += "}";

        if (!outDir.empty() &&
            !writeWav(outDir + "/seed-" + std::to_string(seeds[s]) + ".wav",
                      audio, channels, rate))
        {
            fprintf(stderr, "%s: cannot write under %s\n", argv[0],
                    outDir.c_str());
            return 1;
        }
    }

    out += "]}";
    printf("%s\n", out.c_str());

    return 0;
}
