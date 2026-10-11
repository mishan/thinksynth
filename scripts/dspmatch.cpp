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
 */

/*
 * dspmatch -- can a search hear its way back to a patch?
 *
 * A search judged on its audio is two things that fail differently: a
 * distance between sounds, and something that walks downhill on it. When
 * the pair fails to match a recording there is no telling which was at
 * fault, or whether the synth could have made the sound at all. So the
 * targets here are renders of patches that ship: reachable by construction,
 * with the answer known. Three questions, in the order they have to be
 * answered, and then the thing itself:
 *
 *   near     Does the distance agree with an ear about which sounds are
 *            alike? Renders every file given and lists each one's nearest
 *            neighbors. A kick should land next to the other kicks.
 *
 *   probe    Is the distance something a search can walk on? Moves each
 *            constant of one patch a step at a time either side of where
 *            it is and measures how far the sound went. A constant is
 *            `smooth' if further is always further, `rugged' if not, and
 *            `deaf' if nothing moved -- which is a gene a search wastes
 *            draws on.
 *
 *   recover  Does it close? Scrambles a patch's constants, then runs a
 *            (1+lambda) evolution strategy against the render of the
 *            original. The distance should come back towards zero. The
 *            constants need not come back to where they were: two
 *            settings that sound alike are one answer, not an error.
 *
 *   match    The same search with the target read from a WAV file and the
 *            constants starting where the patch has them: how near can this
 *            graph get to that sound. `-o' names where the answer goes.
 *
 *   grow     match, and then the graph itself: a node from the pool spliced
 *            into a wire, or an envelope or an LFO put on a constant,
 *            wherever one can go, kept if the sound is nearer for it.
 *            `pool' lists what may be spliced in.
 *
 * The genes and the edits are src/NodeMutate.h's; this is the search over
 * them, and the argument for each of their rules.
 *
 * A gene is a constant the author wrote down -- `node { arg = 0.3; }' or a
 * top-level `@control = 0.3;' -- and an edit is NodeEdit::Text splicing the
 * patch's own text, so every candidate is a .dsp that loads, diffs and
 * opens in the editor.
 *
 * A gene moves in a coordinate where a step is about as audible anywhere.
 * A time or a frequency moves in octaves: from 2 ms, 50 ms away is a
 * different instrument and from 400 ms it is a nudge, and a search stepping
 * by a fraction of a 0-500 ms range cannot land on a 2 ms attack at all.
 * Anything written with a unit is taken for one of those, and so is an
 * unranged constant, and so is a control sitting in the bottom tenth of a
 * range that starts at zero -- an LFO rate of 0.5 in 0 to 10 is the usual
 * one, which a step of a twentieth of the range doubles. The rest move across their declared range.
 * A constant of zero with no range has neither and is left alone.
 *
 * Some constants are not quantities. A waveform is one of six, a tap count
 * is a whole number, and an FM operator's ratio is a quantity only on
 * paper: 2 is an octave, 2.1 is a bell, and between them the distance does
 * not slope towards either. These are choice genes -- one of a short list.
 * Nothing in a plugin says "this ratio tunes an oscillator", so which args
 * those are is a table here; it wants to be something a plugin declares.
 *
 * A choice cannot be judged by making it. By the time a search has been
 * running a while its quantities are tuned around the choices it has, and
 * the right waveform dropped into a patch tuned for the wrong one sounds
 * worse than the wrong one does. Both of the obvious schemes were tried --
 * a probability per choice learned from each generation's winners, and
 * rendering the search's mean with one choice flipped -- and both keep
 * whatever a gene started as. So a choice is judged after it has been tuned
 * for: each alternative gets a short search of its own from where the best
 * patch stands, the incumbent gets the same so that it is not beaten by the
 * extra tuning alone, and the winner is where the next gene starts from. A
 * graph edit is a choice in this sense too, and would be judged the same
 * way.
 *
 * A graph edit is judged as a choice is, and costs what a choice costs, so
 * it is raced in two heats: every candidate gets a few generations, which
 * is enough to tell a filter on the note number from a filter on the
 * signal, and the few that come through get a trial of the full length
 * beside the graph as it stands. An edit has to win by a margin, not by a
 * hair. A bigger graph always fits a little better, and a node that buys a
 * hundredth of a dB is a node someone has to read.
 *
 * The edits are text edits -- NodeEdit::Text adds the node and moves the
 * wire -- so a grown patch is a .dsp with one more block in it, and what it
 * grew is a diff. A modulation is three blocks, since the editor writes
 * wires and not arithmetic: the modulator at unit amplitude, a math::mul
 * for the depth and a math::add holding what the constant was, and the
 * arg now reads the sum. `cutoff = 700 + env->out * 300' is what a person
 * would write, and folding the three back into that line is a later pass.
 *
 * The search is a separable CMA-ES: a step size per gene, learned from
 * which steps paid. One step size for every gene was tried first and
 * stalls a dB or two out, because a gene the sound hangs on and a gene it
 * barely hears want steps a hundred times apart. Separable, so without the
 * covariances that would let it walk along a trade between two controls;
 * that is the next thing to add if runs stall on one.
 *
 * Candidates are scored in forked children, one each. A thSynth is not
 * something two threads can each have one of, and a process is.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "think.h"
#include "thcRandom.h"

#include "thSoundFeat.h"
#include "thSoundFile.h"

#include "NodeEdit.h"
#include "NodeEvolve.h"
#include "NodeMutate.h"

using namespace NodeMutate;
using namespace NodeEvolve;

using thsound::Distance;
using thsound::Extractor;
using thsound::Features;
using thsound::readWav;
using thsound::writeWav;
using thsound::detectNote;

/* How long a note is auditioned; -w changes the hold. */
static int HOLD_WINDOWS = thsound::HOLD_WINDOWS;
static const int TAIL_WINDOWS = thsound::TAIL_WINDOWS;

/* Which term probe prints: a sum cannot say which of its parts misbehaved. */
static string probeTerm = "total";
static bool verbose = false;

static const int PROBE_STEPS = 4;
static const double SCRAMBLE_STEPS = 6;

static string pluginPath;
static string scratchPath;
static int note = -1;







static bool readFile (const char *path, string &out)
{
    std::ifstream in(path);

    if (!in)
        return false;

    std::ostringstream ss;

    ss << in.rdbuf();
    out = ss.str();

    return true;
}

/* One note of a patch's text, mixed to mono. Through a scratch file because
   loadTree onto a channel takes a filename; a search that did this in
   earnest would want the FILE* overload taught about channels. */
static bool render (const string &source, vector<float> &mono,
                    vector<Gene> *genes = NULL, Shape *shape = NULL)
{
    {
        std::ofstream out(scratchPath.c_str(), std::ios::trunc);

        out << source;

        if (!out)
            return false;
    }

    srand(1);

    thSynth synth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thSynthTree *tree = synth.loadTree(scratchPath, 0, 100);

    if (tree == NULL)
        return false;

    if (genes || shape)
        NodeMutate::read(tree, source, genes, shape);

    /* Loaded a second time by renderNote, onto the same channel; the
       first load is what the genes and the shape were read from. */
    return thsound::renderNote(synth, scratchPath,
                               vector<std::pair<string, float> >(), note,
                               HOLD_WINDOWS, TAIL_WINDOWS, mono);
}

/* `source' and what it sounds like, beside each other. */
static void save (const string &prefix, const string &source)
{
    vector<float> mono;

    std::ofstream((prefix + ".dsp").c_str()) << source;

    if (render(source, mono))
        writeWav(prefix + ".wav", mono);
}







static double score (const Extractor &ex, const Features &target,
                     const string &source, Distance *parts = NULL)
{
    vector<float> mono;
    Features f;

    if (!render(source, mono))
        return DEAD;

    ex.extract(mono, f);

    if (!f.usable())
        return DEAD;

    const Distance d = Extractor::distance(target, f);

    if (parts)
        *parts = d;

    return d.total();
}

/* score() for each of `sources', each in a child of its own with a
   scratch file of its own. A child that dies scores as a patch that would
   not load, which for a search is what it was. */
static void scoreAll (const Extractor &ex, const Features &target,
                      const vector<string> &sources, vector<double> &out)
{
    vector<pid_t> pids(sources.size(), -1);
    vector<int> fds(sources.size(), -1);

    out.assign(sources.size(), DEAD);

    for (size_t k = 0; k < sources.size(); k++)
    {
        int fd[2];

        if (pipe(fd) != 0)
            continue;

        pids[k] = fork();

        if (pids[k] == 0)
        {
            char suffix[32];

            close(fd[0]);
            snprintf(suffix, sizeof suffix, ".%d.dsp", (int)k);
            scratchPath += suffix;

            const double d = score(ex, target, sources[k]);
            const ssize_t n = ::write(fd[1], &d, sizeof d);

            unlink(scratchPath.c_str());
            _exit(n == (ssize_t)sizeof d ? 0 : 1);
        }

        close(fd[1]);

        if (pids[k] < 0)
            close(fd[0]);
        else
            fds[k] = fd[0];
    }

    for (size_t k = 0; k < sources.size(); k++)
    {
        if (fds[k] < 0)
            continue;

        double d;

        if (read(fds[k], &d, sizeof d) == (ssize_t)sizeof d && std::isfinite(d))
            out[k] = d;

        close(fds[k]);
        waitpid(pids[k], NULL, 0);
    }
}

/* The search's view of this program: scored here, in forked children a
   generation at a time, and its commentary on stdout. */
static NodeEvolve::Host hostFor (const Extractor &ex, const Features &target)
{
    NodeEvolve::Host h;

    h.score = [&ex, &target] (const string &source, Distance *parts)
              { return score(ex, target, source, parts); };
    h.scoreAll = [&ex, &target] (const vector<string> &sources,
                                 vector<double> &out)
                 { scoreAll(ex, target, sources, out); };
    h.read = [] (const string &source, vector<Gene> *genes, Shape *shape)
             { vector<float> mono; return render(source, mono, genes, shape); };
    h.say = [] (const string &text) { fputs(text.c_str(), stdout); };
    h.verbose = verbose;

    return h;
}

/* ------------------------------------------------------------------ near */

static int near (const Extractor &ex, int argc, char **argv)
{
    vector<string> names;
    vector<Features> feats;

    for (int i = 0; i < argc; i++)
    {
        string source;
        vector<float> mono;
        Features f;

        if (!readFile(argv[i], source) || !render(source, mono))
        {
            printf("skip  %s: would not load\n", argv[i]);
            continue;
        }

        ex.extract(mono, f);

        if (!f.usable())
        {
            printf("skip  %s: %s\n", argv[i], thsound::verdictName(f.verdict));
            continue;
        }

        const char *slash = strrchr(argv[i], '/');

        names.push_back(slash ? slash + 1 : argv[i]);
        feats.push_back(f);
    }

    printf("\n%-22s  %s\n", "patch", "nearest (spectral + envelope/2, dB)");

    for (size_t i = 0; i < feats.size(); i++)
    {
        vector<std::pair<double, size_t> > order;

        for (size_t j = 0; j < feats.size(); j++)
            if (j != i)
                order.push_back(std::make_pair(
                    Extractor::distance(feats[i], feats[j]).total(), j));

        std::stable_sort(order.begin(), order.end());

        printf("%-22s", names[i].c_str());

        for (size_t k = 0; k < 3 && k < order.size(); k++)
            printf("  %s %.1f", names[order[k].second].c_str(), order[k].first);

        printf("\n");
    }

    return 0;
}

/* ----------------------------------------------------------------- probe */

static int probe (const Extractor &ex, const char *file)
{
    string source;
    vector<float> mono;
    vector<Gene> genes;
    Features target;

    if (!readFile(file, source) || !render(source, mono, &genes))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    ex.extract(mono, target);

    if (!target.usable())
    {
        printf("%s: %s\n", file, thsound::verdictName(target.verdict));
        return 2;
    }

    printf("%s: %d genes, self-distance %.4f\n\n", file, (int)genes.size(),
           score(ex, target, source));

    printf("%-28s %9s ", "gene", "value");

    for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
        if (s)
            printf(" %+5d", s);

    printf("\n");

    int smooth = 0, rugged = 0, deaf = 0;

    for (size_t g = 0; g < genes.size(); g++)
    {
        if (!genes[g].choices.empty())
            continue;

        double d[2 * PROBE_STEPS + 1];
        bool refused[2 * PROBE_STEPS + 1];
        double furthest = 0;
        bool monotone = true;

        for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
        {
            string candidate = source;
            const int k = s + PROBE_STEPS;

            d[k] = 0;

            /* An edit the writer refuses is no measurement, which is not
               the same as a measurement of zero. */
            refused[k] = s && !write(candidate, genes[g],
                                     moved(genes[g], genes[g].value, s));

            if (!s || refused[k])
                continue;

            Distance parts;

            d[k] = score(ex, target, candidate, &parts);

            if (d[k] < DEAD)
                d[k] = probeTerm == "spectral" ? parts.spectral
                     : probeTerm == "envelope" ? parts.envelope
                     : probeTerm == "noise"    ? parts.noise
                     : d[k];

            if (d[k] > furthest)
                furthest = d[k];
        }

        /* Further should be no nearer. A tenth of a dB of slack: a range
           wall makes two steps the same step, and that is not ruggedness. */
        for (int s = 1; s < PROBE_STEPS; s++)
            for (int side = -1; side <= 1; side += 2)
            {
                const int in = PROBE_STEPS + side * s;
                const int out = in + side;

                if (!refused[in] && !refused[out] && d[out] < d[in] - 0.1)
                    monotone = false;
            }

        const char *kind = furthest < 0.05 ? "deaf"
                         : (monotone ? "smooth" : "RUGGED");

        if (furthest < 0.05) deaf++;
        else if (monotone) smooth++;
        else rugged++;

        printf("%-28s %9.4g ", geneName(genes[g]).c_str(), genes[g].value);

        for (int s = -PROBE_STEPS; s <= PROBE_STEPS; s++)
            if (s)
            {
                if (refused[s + PROBE_STEPS])
                    printf("   n/a");
                else if (d[s + PROBE_STEPS] >= DEAD)
                    printf("  dead");
                else
                    printf(" %5.1f", d[s + PROBE_STEPS]);
            }

        printf("  %s\n", kind);
    }

    printf("\n%d smooth, %d rugged, %d deaf\n", smooth, rugged, deaf);

    /* A choice has no further and nearer, only elsewhere: how far each of
       the others is from the one the patch made. */
    for (size_t g = 0; g < genes.size(); g++)
    {
        if (genes[g].choices.empty())
            continue;

        printf("\n%s is %g; at", geneName(genes[g]).c_str(), genes[g].value);

        for (size_t c = 0; c < genes[g].choices.size(); c++)
        {
            string candidate = source;

            if (genes[g].choices[c] == genes[g].value)
                continue;

            if (!write(candidate, genes[g], genes[g].choices[c]))
                printf("  %g n/a", genes[g].choices[c]);
            else
            {
                const double d = score(ex, target, candidate);

                if (d >= DEAD)
                    printf("  %g dead", genes[g].choices[c]);
                else
                    printf("  %g %.1f", genes[g].choices[c], d);
            }
        }

        printf("\n");
    }

    return 0;
}

/* --------------------------------------------------------------- recover */


/* The whole search: each choice gene's alternatives raced against the
   incumbent, every one of them tuned for before it is judged, and then the
   quantities at length around the choices that won.

   The race comes first, from wherever the patch starts. Tuning the
   quantities beforehand was tried and is what loses: thirty generations
   fit them to the wrong waveform well enough that the right one cannot
   catch up in a trial, and the search keeps what it began with. A trial
   has to be long enough to tell, too -- at twelve generations the right
   choice and the wrong one finish within noise of each other, and at
   thirty they do not. */

static void report (const vector<Gene> &genes, const char *was,
                    const vector<double> &start, const vector<double> &found)
{
    printf("\n%-28s %10s %10s %10s\n", "gene", "truth", was, "found");

    for (size_t g = 0; g < genes.size(); g++)
        printf("%-28s %10.4g %10.4g %10.4g%s\n", geneName(genes[g]).c_str(),
               genes[g].value, fromU(genes[g], start[g]),
               fromU(genes[g], found[g]),
               !genes[g].choices.empty() ? "  choice"
                                         : (genes[g].octaves ? "  oct" : ""));
}

static int recover (const Extractor &ex, const char *file, unsigned int seed,
                    int generations, const string &prefix)
{
    string truth;
    vector<float> mono;
    vector<Gene> genes;
    Features target;

    if (!readFile(file, truth) || !render(truth, mono, &genes))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    ex.extract(mono, target);

    if (!target.usable() || genes.empty())
    {
        printf("%s: nothing to search\n", file);
        return 2;
    }

    /* A generator of its own for the scramble, so the same seed scrambles
       the same way whatever the search then draws. */
    std::mt19937 rng(seed ^ 0x5eed);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);

    vector<double> start(genes.size());
    string scrambled = truth;

    for (size_t g = 0; g < genes.size(); g++)
    {
        start[g] = genes[g].choices.empty()
            ? toU(genes[g], moved(genes[g], genes[g].value,
                                  uniform(rng) * SCRAMBLE_STEPS))
            : (double)thcUniformIndex(rng, 0, genes[g].choices.size() - 1);

        write(scrambled, genes[g], fromU(genes[g], start[g]));
    }

    printf("%s: %d genes scrambled, seed %u\n\n", file, (int)genes.size(), seed);

    string found;
    vector<double> foundAt;

    const double d = searchAll(hostFor(ex, target), truth, genes, start, seed,
                               generations, found, foundAt);

    report(genes, "scrambled", start, foundAt);
    printf("\nfound %.3f\n", d);

    if (!prefix.empty())
    {
        save(prefix + "-target", truth);
        save(prefix + "-start", scrambled);
        save(prefix + "-found", found);
    }

    return 0;
}

/* ----------------------------------------------------------------- match */

/* The distance between a WAV and a patch as it stands, and where the two
   differ in noisiness: the flatness of each, over the note. */
static int scoreOnly (const Extractor &ex, const char *wav, const char *file)
{
    string source, why;
    vector<float> heard, mono;
    Features target, mine;

    if (!readWav(wav, heard, why) || !readFile(file, source) ||
        !render(source, mono))
    {
        printf("%s / %s: %s\n", wav, file, why.c_str());
        return 2;
    }

    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);
    ex.extract(mono, mine);

    if (!target.usable() || !mine.usable())
        return 2;

    const Distance d = Extractor::distance(target, mine);

    printf("%-40s total %6.2f  spectral %5.2f  envelope %5.2f  noise %5.2f\n",
           file, d.total(), d.spectral, d.envelope, d.noise);

    if (getenv("DSPMATCH_FLATNESS"))
    {
        printf("  flatness dB per 12 ms, target / patch:\n ");

        for (size_t f = 0; f < target.noise.size() && f < mine.noise.size() && f < 24; f++)
            printf(" %4.0f/%-4.0f", target.noise[f], mine.noise[f]);

        printf("\n");
    }

    return 0;
}

static int match (const Extractor &ex, const char *wav, const char *file,
                  unsigned int seed, int generations, const string &prefix)
{
    string source, why;
    vector<float> heard, mono;
    vector<Gene> genes;
    Features target;

    if (!readWav(wav, heard, why))
    {
        printf("%s: %s\n", wav, why.c_str());
        return 2;
    }

    if (note < 0)
    {
        const int heardNote = detectNote(heard);

        note = heardNote >= 0 ? heardNote : 60;
        printf("%s: %s\n", wav, heardNote >= 0 ? "pitched" : "unpitched");
    }

    if (note < 0)
    {
        const int heardNote = detectNote(heard);

        note = heardNote >= 0 ? heardNote : 60;
        printf("%s: %s\n", wav, heardNote >= 0 ? "pitched" : "unpitched");
    }

    if (!readFile(file, source) || !render(source, mono, &genes) || genes.empty())
    {
        printf("%s: would not load, or has nothing to search\n", file);
        return 2;
    }

    /* As long as a render, so both have the same frames: a longer target
       is cut at the end of the release and a shorter one is followed by
       silence, which is what followed it. */
    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);

    if (!target.usable())
    {
        printf("%s: %s\n", wav, thsound::verdictName(target.verdict));
        return 2;
    }

    vector<double> start(genes.size());

    for (size_t g = 0; g < genes.size(); g++)
        start[g] = toU(genes[g], genes[g].value);

    printf("%s towards %s: %d genes, note %d, seed %u\n\n", file, wav,
           (int)genes.size(), note, seed);

    string found;
    vector<double> foundAt;

    const double d = searchAll(hostFor(ex, target), source, genes, start,
                               seed,
                               generations, found, foundAt);

    report(genes, "start", start, foundAt);
    printf("\nfound %.3f\n", d);

    if (!prefix.empty())
    {
        writeWav(prefix + "-target.wav", heard);
        save(prefix + "-start", source);
        save(prefix + "-found", found);
    }

    return 0;
}

/* ------------------------------------------------------------------ grow */


static int showPool (void)
{
    vector<Splice> pool;
    thSynth poolSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);

    buildPool(poolSynth.getPluginManager(), pool);

    for (size_t i = 0; i < pool.size(); i++)
    {
        printf("%-18s %s -> %s ", pool[i].spelling.c_str(),
               pool[i].in.c_str(), pool[i].out.c_str());

        for (size_t k = 0; k < pool[i].initial.size(); k++)
            printf("  %s=%g", pool[i].initial[k].first.c_str(),
                   pool[i].initial[k].second);

        printf("\n");
    }

    return pool.empty() ? 2 : 0;
}










static int growTowards (const Extractor &ex, const char *wav, const char *file,
                        unsigned int seed, int generations, int rounds,
                        const string &prefix)
{
    string source, why, found;
    vector<float> heard, mono;
    Features target;

    if (!readWav(wav, heard, why))
    {
        printf("%s: %s\n", wav, why.c_str());
        return 2;
    }

    if (note < 0)
    {
        const int heardNote = detectNote(heard);

        note = heardNote >= 0 ? heardNote : 60;
        printf("%s: %s\n", wav, heardNote >= 0 ? "pitched" : "unpitched");
    }

    if (note < 0)
    {
        const int heardNote = detectNote(heard);

        note = heardNote >= 0 ? heardNote : 60;
        printf("%s: %s\n", wav, heardNote >= 0 ? "pitched" : "unpitched");
    }

    if (!readFile(file, source) || !render(source, mono))
    {
        printf("%s: would not load\n", file);
        return 2;
    }

    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);

    if (!target.usable())
    {
        printf("%s: %s\n", wav, thsound::verdictName(target.verdict));
        return 2;
    }

    printf("%s towards %s: note %d, seed %u\n\n", file, wav, note, seed);

    vector<Splice> pool;

    {
        thSynth poolSynth(pluginPath, TH_DEFAULT_WINDOW_LENGTH,
                          TH_DEFAULT_SAMPLES);

        buildPool(poolSynth.getPluginManager(), pool);
    }

    const int rc = grow(hostFor(ex, target), pool, source, seed, generations,
                        rounds, found);

    if (rc == 0 && !prefix.empty())
    {
        writeWav(prefix + "-target.wav", heard);
        save(prefix + "-start", source);
        save(prefix + "-found", found);
    }

    return rc;
}

int main (int argc, char **argv)
{
    unsigned int seed = 1;
    int generations = 150;
    int rounds = 3;
    string prefix;
    int i = 1;

    for (; i < argc && argv[i][0] == '-'; i++)
    {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) pluginPath = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = (unsigned int)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) generations = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) note = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) HOLD_WINDOWS = std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) probeTerm = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else break;
    }

    if (pluginPath.empty() || i >= argc ||
        (i + 1 >= argc && strcmp(argv[i], "pool")))
    {
        printf("usage: %s -p PLUGINS [options] near file.dsp ...\n"
               "       %s -p PLUGINS [options] [-t spectral|envelope|noise] probe file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] recover file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] match target.wav file.dsp\n"
               "       %s -p PLUGINS [options] [-o PREFIX] [-r ROUNDS] grow target.wav file.dsp\n"
               "       %s -p PLUGINS [options] score target.wav file.dsp\n"
               "       %s -p PLUGINS pool\n"
               "  -n NOTE         the note played; match and grow detect it from the target,\n"
               "                  the rest play 60\n"
               "  -w HOLD         how long it is held, in windows of 1024 frames\n"
               "  -s SEED  -g GENERATIONS\n"
               "  -o PREFIX       write PREFIX-target, -start and -found, as .wav\n"
               "                  and, where there is a patch, as .dsp\n",
               argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

    if (pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    char scratch[] = "/tmp/dspmatch-XXXXXX.dsp";
    const int fd = mkstemps(scratch, 4);

    if (fd < 0)
    {
        perror("mkstemps");
        return 2;
    }

    close(fd);
    scratchPath = scratch;

    const Extractor ex(TH_DEFAULT_SAMPLES);
    const string mode = argv[i++];

    if (note < 0 && mode != "match" && mode != "grow")
        note = 60;
    int rc = 2;

    if (mode == "near")
        rc = near(ex, argc - i, argv + i);
    else if (mode == "probe")
        rc = probe(ex, argv[i]);
    else if (mode == "recover")
        rc = recover(ex, argv[i], seed, generations, prefix);
    else if (mode == "pool")
        rc = showPool();
    else if (mode == "grow" && i + 1 < argc)
        rc = growTowards(ex, argv[i], argv[i + 1], seed, generations, rounds, prefix);
    else if (mode == "score" && i + 1 < argc)
        rc = scoreOnly(ex, argv[i], argv[i + 1]);
    else if (mode == "match" && i + 1 < argc)
        rc = match(ex, argv[i], argv[i + 1], seed, generations, prefix);

    unlink(scratch);

    return rc;
}
