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

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <exception>
#include <memory>

#include "thcRandom.h"
#include "think.h"
#include "thSoundFile.h"

#include "NodeEvolve.h"

using namespace NodeMutate;
using thsound::Distance;

namespace NodeEvolve {

/* printf, into the host's commentary. */
static void say (const Host &host, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void say (const Host &host, const char *fmt, ...)
{
    if (!host.say)
        return;

    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    string text(n > 0 ? (size_t)n : 0, '\0');

    va_start(ap, fmt);
    vsnprintf(&text[0], text.size() + 1, fmt, ap);
    va_end(ap);

    host.say(text);
}

static bool stopped (const Host &host)
{
    return host.stop && host.stop();
}

/* A separable CMA-ES over the quantities among `genes', from `mean' in
   their coordinates, towards `target'. Choice genes stay where `mean' has
   them. `base' is only the text the writes go into: every gene is rewritten
   every time. Leaves the best patch heard in `bestSource' and where its
   genes were in `bestAt'. `label' heads a printed trace, and no label is no
   trace. */
double search (const Host &host,
                      const string &base, const vector<Gene> &genes,
                      vector<double> mean, double sigma, std::mt19937 &rng,
                      int generations, const char *label,
                      string &bestSource, vector<double> &bestAt)
{
    const size_t n = genes.size();

    thcNormal normal;

    /* Hansen's defaults, with the learning rates of the diagonal raised by
       (n + 2) / 3 as Ros and Hansen's separable variant does: n variances
       are learned from what would otherwise teach n^2 / 2 covariances. */
    const int lambda = 16;
    const int mu = lambda / 2;

    vector<double> weight(mu);
    double weightSum = 0, weightSq = 0;

    for (int i = 0; i < mu; i++)
    {
        weight[i] = log(mu + 0.5) - log(i + 1.0);
        weightSum += weight[i];
    }

    for (int i = 0; i < mu; i++)
    {
        weight[i] /= weightSum;
        weightSq += weight[i] * weight[i];
    }

    /* N is the dimension CMA-ES's constants are functions of, and a patch
       of nothing but choices still has to divide by it. */
    vector<size_t> quantity;

    for (size_t g = 0; g < n; g++)
        if (genes[g].choices.empty())
            quantity.push_back(g);

    const double N = (double)std::max<size_t>(quantity.size(), 1);
    const double muEff = 1.0 / weightSq;
    const double cSigma = (muEff + 2) / (N + muEff + 5);
    const double dSigma = 1 + cSigma +
        2 * std::max(0.0, sqrt((muEff - 1) / (N + 1)) - 1);
    const double cC = (4 + muEff / N) / (N + 4 + 2 * muEff / N);
    const double boost = (N + 2) / 3;
    const double c1 = std::min(1.0, boost * 2 / ((N + 1.3) * (N + 1.3) + muEff));
    const double cMu = std::min(1 - c1,
        boost * 2 * (muEff - 2 + 1 / muEff) / ((N + 2) * (N + 2) + muEff));
    const double chiN = sqrt(N) * (1 - 1 / (4 * N) + 1 / (21 * N * N));

    vector<double> var(n, 1.0), pathSigma(n, 0.0), pathC(n, 0.0);

    bestSource = base;
    bestAt = mean;

    for (size_t g = 0; g < n; g++)
        write(bestSource, genes[g], fromU(genes[g], mean[g]));

    Distance parts;
    double best = host.score(bestSource, &parts);

    if (label)
    {
        say(host, "%s\n%5s %9s %9s %9s %9s %6s\n", label, "gen", "best", "spectral",
               "envelope", "noise", "sigma");
        say(host, "%5d %9.3f %9.3f %9.3f %9.3f\n", 0, best, parts.spectral,
               parts.envelope, parts.noise);
    }

    if (quantity.empty())
        return best;

    for (int gen = 1; gen <= generations && best > 0.01 && !stopped(host);
         gen++)
    {
        vector<vector<double> > at(lambda, mean);
        vector<string> sources(lambda, base);
        vector<double> scores;

        for (int k = 0; k < lambda; k++)
            for (size_t g = 0; g < n; g++)
            {
                if (!genes[g].choices.empty())
                {
                    write(sources[k], genes[g], fromU(genes[g], mean[g]));
                    continue;
                }

                const double u = mean[g] + sigma * sqrt(var[g]) * normal(rng);

                /* Where it landed, not where it was thrown: a sample past
                   a range wall is the wall, and the update should learn
                   from the point that was heard. */
                at[k][g] = toU(genes[g], fromU(genes[g], u));

                if (!write(sources[k], genes[g], fromU(genes[g], u)))
                    at[k][g] = mean[g];
            }

        host.scoreAll(sources, scores);

        vector<std::pair<double, int> > order;

        for (int k = 0; k < lambda; k++)
            order.push_back(std::make_pair(scores[k], k));

        std::stable_sort(order.begin(), order.end());

        if (order[0].first < best)
        {
            best = order[0].first;
            bestSource = sources[order[0].second];
            bestAt = at[order[0].second];
        }

        double pathNorm = 0;
        vector<double> step(n, 0.0);

        for (size_t q = 0; q < quantity.size(); q++)
        {
            const size_t g = quantity[q];

            for (int i = 0; i < mu; i++)
                step[g] += weight[i] * (at[order[i].second][g] - mean[g]) / sigma;

            pathSigma[g] = (1 - cSigma) * pathSigma[g] +
                sqrt(cSigma * (2 - cSigma) * muEff) * step[g] / sqrt(var[g]);
            pathNorm += pathSigma[g] * pathSigma[g];
        }

        pathNorm = sqrt(pathNorm);

        const bool moving = pathNorm /
            sqrt(1 - pow(1 - cSigma, 2.0 * gen)) < (1.4 + 2 / (N + 1)) * chiN;

        for (size_t q = 0; q < quantity.size(); q++)
        {
            const size_t g = quantity[q];

            pathC[g] = (1 - cC) * pathC[g] +
                (moving ? sqrt(cC * (2 - cC) * muEff) * step[g] : 0.0);

            double rankMu = 0;

            for (int i = 0; i < mu; i++)
            {
                const double y = (at[order[i].second][g] - mean[g]) / sigma;

                rankMu += weight[i] * y * y;
            }

            var[g] = (1 - c1 - cMu) * var[g] +
                c1 * (pathC[g] * pathC[g] +
                      (moving ? 0.0 : cC * (2 - cC) * var[g])) +
                cMu * rankMu;

            mean[g] += sigma * step[g];
        }

        sigma *= exp(cSigma / dSigma * (pathNorm / chiN - 1));

        if (label && (gen % 10 == 0 || gen == generations))
        {
            host.score(bestSource, &parts);
            say(host, "%5d %9.3f %9.3f %9.3f %9.3f %6.2f\n", gen, best,
                   parts.spectral, parts.envelope, parts.noise, sigma);
        }
    }

    return best;
}

double searchAll (const Host &host,
                         const string &base, const vector<Gene> &genes,
                         const vector<double> &start, unsigned int seed,
                         int generations, string &bestSource,
                         vector<double> &bestAt)
{
    const int TRIAL = 30, ROUNDS = 2;

    std::mt19937 rng(seed);

    bestAt = start;
    bestSource = base;

    for (size_t g = 0; g < genes.size(); g++)
        write(bestSource, genes[g], fromU(genes[g], start[g]));

    double best = host.score(bestSource, NULL);

    say(host, "start %.3f\n", best);

    for (int round = 0; round < ROUNDS && !stopped(host); round++)
    {
        bool changed = false;

        for (size_t g = 0; g < genes.size(); g++)
        {
            if (genes[g].choices.size() < 2)
                continue;

            /* The same arg of the same kind on other nodes, with the
               same list: seven oscillators' waveforms. Raced once as
               one gene as well as each on its own, since one saw of
               seven turning square is inaudible and all seven is a
               different instrument. Run from the first of the group. */
            vector<size_t> group;

            for (size_t h = 0; h < genes.size(); h++)
                if (!genes[h].node.empty() && !genes[g].node.empty() &&
                    genes[h].arg == genes[g].arg && genes[h].choices == genes[g].choices)
                    group.push_back(h);

            const bool leads = group.size() > 1 && group[0] == g;

            for (int together = 0; together < (leads ? 2 : 1); together++)
            {
            const vector<double> from = bestAt;

            if (together)
                say(host, "\nall %d %s:", (int)group.size(), genes[g].arg.c_str());
            else
                say(host, "\n%s:", geneName(genes[g]).c_str());

            for (size_t c = 0; c < genes[g].choices.size() && !stopped(host);
                 c++)
            {
                vector<double> trial = from, trialAt;
                string trialSource;

                trial[g] = (double)c;

                if (together)
                    for (size_t h = 0; h < group.size(); h++)
                        trial[group[h]] = (double)c;

                const double d = search(host, base, genes, trial,
                                        round ? 1.0 : 2.0, rng, TRIAL, NULL,
                                        trialSource, trialAt);

                say(host, "  %g%s %.2f", genes[g].choices[c],
                       c == (size_t)lrint(from[g]) ? "*" : "", d);

                if (d < best)
                {
                    changed |= c != (size_t)lrint(from[g]);
                    best = d;
                    bestSource = trialSource;
                    bestAt = trialAt;
                }
            }

            say(host, "\n");
            }
        }

        if (!changed)
            break;
    }

    string polished;
    vector<double> polishedAt;

    if (stopped(host))
        return best;

    say(host, "\n");

    const double d = search(host, base, genes, bestAt, 1.0, rng,
                            generations, "closing", polished, polishedAt);

    if (d < best)
    {
        best = d;
        bestSource = polished;
        bestAt = polishedAt;
    }

    return best;
}

/* `generations' more of tuning for a candidate, from where it stands. */
void tune (const Host &host, Candidate &c,
                  std::mt19937 &rng, int generations)
{
    string found;
    vector<double> at;

    const double d = search(host, c.source, c.genes, c.at, 1.0, rng,
                            generations, NULL, found, at);

    if (d < c.score)
    {
        c.score = d;
        c.source = found;
        c.at = at;
    }
}

bool candidate (const Host &host, const string &source, const string &what,
                Candidate &c, Shape *shape)
{
    c.what = what;
    c.source = source;
    c.genes.clear();
    c.score = DEAD;

    if (!host.read(source, &c.genes, shape) || c.genes.empty())
        return false;

    c.at.resize(c.genes.size());

    for (size_t g = 0; g < c.genes.size(); g++)
        c.at[g] = toU(c.genes[g], c.genes[g].value);

    return true;
}

string tuneAdded (thSynth &synth, const string &from, const string &text,
                  int generations,
                  const std::function<double (const string &)> &distance)
{
    static std::atomic<unsigned int> serial { 0 };
    Host host;

    host.score = [&] (const string &source, Distance *)
    {
        return distance(source);
    };
    host.scoreAll = [&] (const vector<string> &sources, vector<double> &out)
    {
        out.resize(sources.size());

        for (size_t i = 0; i < sources.size(); i++)
            out[i] = distance(sources[i]);
    };
    host.read = [&] (const string &source, vector<Gene> *genes, Shape *shape)
    {
        const string name = "tune#" + std::to_string(++serial) + ".dsp";
        thUtil::GraphHold hold = thUtil::addGraphText(name, source);
        thSynthTree *tree = synth.parseTree(name);

        if (tree == NULL)
            return false;

        NodeMutate::read(tree, source, genes, shape);
        delete tree;

        return true;
    };

    Shape before;
    Candidate c;

    if (!host.read(from, NULL, &before) || !candidate(host, text, "", c))
        return text;

    /* Only what the edit brought: the rest is the graph the piece has. */
    vector<Gene> added;
    vector<double> at;

    for (size_t g = 0; g < c.genes.size(); g++)
        if (!c.genes[g].node.empty() &&
            std::find(before.names.begin(), before.names.end(),
                      c.genes[g].node) == before.names.end())
        {
            added.push_back(c.genes[g]);
            at.push_back(c.at[g]);
        }

    if (added.empty())
        return text;

    std::mt19937 rng(1);
    string found;
    vector<double> bestAt;

    search(host, text, added, at, 1.0, rng, generations, NULL, found, bestAt);

    return found.empty() ? text : found;
}

int grow (const Host &host, const vector<Splice> &pool,
          const string &start, unsigned int seed, int generations,
          int rounds, string &found)
{
    const int HEAT = 8, FINAL = 30, FINALISTS = 6;

    /* An edit has to be a tenth better and a quarter of a dB better, and
       under half a dB there is nothing left for a node to explain. The
       ratio alone is not enough: at 0.2 dB a tenth is 0.02, which is what
       two runs of the same tuning differ by, and the first version of this
       kept a resonant filter on the `play' gate for it. */
    const double RATIO = 0.9, ABSOLUTE = 0.25, CLOSE = 0.5;

    std::mt19937 rng(seed);

    Candidate best;

    if (!candidate(host, start, "as it stands", best))
        return 2;

    /* Not tuned first. Tuning the graph as it stands before the race
       was tried and is what loses: thirty generations fit the filter to
       what the missing saturator did, and the saturator, once spliced,
       gains too little in a heat to be kept. The graph as it stands
       races with the same tuning as every edit, and the round's winner
       is tuned only once it has won. */
    best.score = host.score(best.source, NULL);
    say(host, "start %.3f\n", best.score);

    for (int round = 1; round <= rounds && best.score > CLOSE &&
                       !stopped(host); round++)
    {
        Shape shape;
        Candidate stands;

        candidate(host, best.source, "as it stands", stands, &shape);
        stands.score = best.score;

        vector<Candidate> heat;
        int pitches = 0;
        const vector<Edit> all = edits(best.source, shape, best.genes, pool,
                                       &pitches);

        for (size_t e = 0; e < all.size() && !stopped(host); e++)
        {
            Candidate c;

            if (candidate(host, all[e].source, all[e].what, c))
                heat.push_back(c);
        }

        say(host, "\nround %d: %d wires, %d splices, %d slots, %d voices, %d candidates\n",
               round, (int)shape.wires.size(), (int)pool.size(),
               (int)shape.slots.size(), pitches, (int)heat.size());

        /* Every candidate gets the whole heat. Culling after three
           generations was tried and dropped the right answer: a
           saturator is a dB behind a stray LFO until it is tuned, and
           the saving was not measurable. */
        for (size_t i = 0; i < heat.size() && !stopped(host); i++)
        {
            heat[i].score = host.score(heat[i].source, NULL);

            const double before = heat[i].score;

            tune(host, heat[i], rng, HEAT);

            if (host.verbose)
                say(host, "  heat %6.3f -> %6.3f  %s\n", before, heat[i].score,
                       heat[i].what.c_str());
        }

        std::stable_sort(heat.begin(), heat.end());

        /* Stopped mid-heat: the round is not finished, and the graph as
           it stood before it is the answer. */
        if (stopped(host))
            break;

        if (heat.size() > (size_t)FINALISTS)
            heat.resize(FINALISTS);

        /* The graph as it stands runs the final too. Without that an edit
           that does nothing wins on the extra tuning. */
        tune(host, stands, rng, HEAT + FINAL);

        for (size_t i = 0; i < heat.size(); i++)
        {
            const double after = heat[i].score;

            tune(host, heat[i], rng, FINAL);
            say(host, "  %6.3f -> %6.3f  %s\n", after, heat[i].score,
                   heat[i].what.c_str());
        }

        say(host, "  %6s    %6.3f  as it stands\n", "", stands.score);

        std::stable_sort(heat.begin(), heat.end());

        if (heat.empty() || heat[0].score > stands.score * RATIO ||
            heat[0].score > stands.score - ABSOLUTE)
        {
            best = stands;
            say(host, "nothing wins by enough\n");
            break;
        }

        best = heat[0];
        say(host, "kept: %s\n", best.what.c_str());
    }

    say(host, "\n");

    vector<double> at;

    if (stopped(host))
    {
        found = best.source;
        return 0;
    }

    const double d = searchAll(host, best.source, best.genes, best.at,
                               seed, generations, found, at);

    say(host, "\nfound %.3f\n", d);

    return 0;
}

Job::~Job (void)
{
    stop_ = true;

    if (thread_.joinable())
        thread_.join();
}

bool Job::start (const Spec &spec, string &why)
{
    if (thread_.joinable())
    {
        why = "a search is already running";
        return false;
    }

    if (spec.targetGraph.empty() && spec.targetWav.empty())
    {
        why = "nothing to move toward";
        return false;
    }

    thread_ = std::thread(&Job::run, this, spec);

    return true;
}

string Job::progress (void) const
{
    std::lock_guard<std::mutex> hold(lock_);

    return line_;
}

double Job::started (void) const
{
    std::lock_guard<std::mutex> hold(lock_);

    return started_;
}

double Job::best (void) const
{
    std::lock_guard<std::mutex> hold(lock_);

    return best_;
}

bool Job::result (string &graph, double &distance, string &why) const
{
    std::lock_guard<std::mutex> hold(lock_);

    graph = found_;
    distance = distance_;
    why = why_;

    return done_ && ok_;
}

/* The worker thread. Nothing thrown here may reach the thread's edge, which
   would end the program and the piece playing in it. */
void Job::run (Spec spec)
{
    try
    {
        work(spec);
    }
    catch (const std::exception &e)
    {
        std::lock_guard<std::mutex> hold(lock_);

        ok_ = false;
        why_ = string("the search failed: ") + e.what();
        done_ = true;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> hold(lock_);

        ok_ = false;
        why_ = "the search failed";
        done_ = true;
    }
}

void Job::work (const Spec &spec)
{
    /* A synth and an extractor for each of the threads a generation is
       scored on. Two synths share nothing: even the plugins that keep a
       table for a whole synth keep one per synth. */
    const unsigned int cores = std::thread::hardware_concurrency();
    const size_t workers = std::max(1u, std::min(8u, cores > 1 ? cores - 1
                                                               : 1u));
    vector<std::unique_ptr<thSynth> > synths;
    vector<std::unique_ptr<thsound::Extractor> > exs;

    for (size_t w = 0; w < workers; w++)
    {
        synths.emplace_back(new thSynth(spec.pluginPath,
                                        TH_DEFAULT_WINDOW_LENGTH,
                                        (int)spec.rate));
        exs.emplace_back(new thsound::Extractor((int)spec.rate));
    }

    thSynth &synth = *synths[0];
    const thsound::Extractor &ex = *exs[0];
    thsound::Features target;
    std::atomic<unsigned int> serial { 0 };

    const auto finish = [this] (bool ok, const string &found, double d,
                                const string &why)
    {
        std::lock_guard<std::mutex> hold(lock_);

        ok_ = ok;
        found_ = found;
        distance_ = d;
        why_ = why;
        done_ = true;
    };

    /* The note a recording sounds, where it has one; middle C otherwise.
       Read before anything is rendered, since the render is at that note. */
    vector<float> recording;
    string unread;

    if (!spec.targetWav.empty() &&
        !thsound::readWav(spec.targetWav, recording, unread, spec.rate))
    {
        finish(false, "", 0, unread);
        return;
    }

    const int detected = recording.empty()
                       ? -1 : thsound::detectNote(recording, spec.rate);
    const int at = detected >= 0 ? detected : 60;

    /* `text' rendered as one note on `on' with `args', under a name of its
       own in the registry inline graphs use. */
    const auto render = [&] (thSynth &on, const string &text,
                             const vector<pair<string, float> > &args,
                             vector<float> &mono)
    {
        const string name = "evolve#" + std::to_string(++serial) + ".dsp";
        thUtil::GraphHold hold = thUtil::addGraphText(name, text);

        return thsound::renderNote(on, name, args, at, thsound::HOLD_WINDOWS,
                                   thsound::TAIL_WINDOWS, mono);
    };

    /* The target as features, at the length a render is. */
    vector<float> mono;

    srand(1);

    if (!render(synth, spec.start, spec.startArgs, mono))
    {
        finish(false, "", 0, "the graph to evolve does not load");
        return;
    }

    vector<float> heard = recording;

    srand(1);

    if (!spec.targetGraph.empty() &&
        !render(synth, spec.targetGraph, spec.targetArgs, heard))
    {
        finish(false, "", 0, "the target graph does not load");
        return;
    }

    heard.resize(mono.size(), 0.0f);
    ex.extract(heard, target);

    if (!target.usable())
    {
        finish(false, "", 0, string("the target is ") +
               thsound::verdictName(target.verdict));
        return;
    }

    const auto scoreOn = [&] (size_t w, const string &source,
                              thsound::Distance *parts)
    {
        vector<float> got;
        thsound::Features f;

        if (!render(*synths[w], source, spec.startArgs, got))
            return DEAD;

        exs[w]->extract(got, f);

        if (!f.usable())
            return DEAD;

        const thsound::Distance d = thsound::Extractor::distance(target, f);

        if (parts != NULL)
            *parts = d;

        std::lock_guard<std::mutex> hold(lock_);

        if (best_ < 0 || d.total() < best_)
            best_ = d.total();

        return d.total();
    };

    Host host;

    host.score = [&] (const string &source, thsound::Distance *parts)
    {
        srand(1);
        return scoreOn(0, source, parts);
    };
    host.scoreAll = [&] (const vector<string> &sources, vector<double> &out)
    {
        out.assign(sources.size(), DEAD);

        /* Each thread takes every workers'th candidate, until a cancel.
           What one throws is rethrown here once all are joined, for run()
           to report; a thread that cannot be started leaves its share to
           this one. */
        std::exception_ptr failed;
        const auto share = [&] (size_t w)
        {
            try
            {
                for (size_t k = w; k < sources.size() && !stop_; k += workers)
                    out[k] = scoreOn(w, sources[k], NULL);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> hold(lock_);

                if (!failed)
                    failed = std::current_exception();
            }
        };

        vector<std::thread> threads;
        size_t w = 1;

        try
        {
            for (; w < workers; w++)
                threads.emplace_back(share, w);
        }
        catch (...)
        {
        }

        share(0);

        for (size_t left = w; left < workers; left++)
            share(left);

        for (std::thread &t : threads)
            t.join();

        if (failed)
            std::rethrow_exception(failed);
    };
    host.read = [&] (const string &source, vector<Gene> *genes, Shape *shape)
    {
        const string name = "evolve#" + std::to_string(++serial) + ".dsp";
        thUtil::GraphHold hold = thUtil::addGraphText(name, source);
        thSynthTree *tree = synth.parseTree(name);

        if (tree == NULL)
            return false;

        NodeMutate::read(tree, source, genes, shape);
        delete tree;

        return true;
    };
    host.say = [this] (const string &text)
    {
        std::lock_guard<std::mutex> hold(lock_);

        /* The last whole line, for a status bar. */
        partial_ += text;

        const size_t nl = partial_.find_last_of('\n');

        if (nl == string::npos)
            return;

        const size_t from = partial_.find_last_of('\n', nl ? nl - 1 : 0);
        const string line = partial_.substr(from == string::npos ? 0
                                                                 : from + 1,
                                            nl - (from == string::npos
                                                  ? 0 : from + 1));

        if (!line.empty())
            line_ = line;

        partial_.erase(0, nl + 1);
    };
    host.stop = [this] { return stop_.load(); };

    {
        const double d0 = host.score(spec.start, NULL);
        std::lock_guard<std::mutex> hold(lock_);

        started_ = d0;
    }

    string found;
    double d = DEAD;

    if (spec.grow)
    {
        vector<Splice> pool;

        buildPool(synth.getPluginManager(), pool);

        if (NodeEvolve::grow(host, pool, spec.start, spec.seed,
                             spec.generations, spec.rounds, found) != 0)
        {
            finish(false, "", 0, "the graph has nothing to evolve");
            return;
        }

        d = host.score(found, NULL);
    }
    else
    {
        Candidate c;
        vector<double> at;

        if (!candidate(host, spec.start, "", c))
        {
            finish(false, "", 0, "the graph has nothing to tune");
            return;
        }

        d = searchAll(host, c.source, c.genes, c.at, spec.seed,
                      spec.generations, found, at);
    }

    finish(true, found, d, stop_ ? "stopped" : "");
}

}
