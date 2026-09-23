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
 * statecheck -- the three nodes an expression cannot be, measured.
 *
 * `misc::slew', `filt::svf' and `osc::noise' are what is left of the node
 * list once arithmetic over signals exists: each one carries state from one
 * sample to the next, which is precisely what a tree of math:: nodes cannot
 * do. State is also what makes them worth a harness. A plugin whose output
 * is a function of its inputs is checked by reading it; one that remembers
 * has a step response, a frequency response and a window boundary, and none
 * of those is visible in a render of the corpus.
 *
 * What is claimed here, in the order the plugins are checked:
 *
 *   misc::slew   reaches 63% of a step at `time' and 98% at four times it,
 *                which is what makes `time' a time constant rather than a
 *                duration; `time = 0' is a wire.
 *
 *   filt::svf    passes DC through its low output and stops it at its high
 *                one; its band output peaks at 1 at the cutoff whatever the
 *                resonance is; its three outputs sum to the input, which is
 *                the property that makes them mixable; and it stays finite
 *                at every corner of cutoff against resonance, which the
 *                other filters in the tree only manage because they are
 *                clamped into it.
 *
 *   osc::noise   is the same noise twice from two fresh synths, stays
 *                inside `amp', and gets darker from white to pink to brown
 *                -- measured as the size of the step between one sample and
 *                the next, which is a high-pass and so a spectral tilt with
 *                no FFT in it.
 *
 * And, for all three, that a window boundary is not an event: the same
 * samples come out cut into windows of one as into windows of five hundred.
 * That is hostcheck's property, which every stateful plugin has to have and
 * which nothing else would catch here -- a filter that kept its history in a
 * local would pass every other check on this list.
 *
 * Graphs are built in C++ rather than parsed, the way hostcheck does it: the
 * inputs wanted here are step functions and steady sines at named
 * frequencies, and none of them is a thing the corpus contains.
 *
 *     scripts/statecheck -p build/plugins/
 *
 * Exit status is the number of failures.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include "thUtil.h"

#include "think.h"

using std::string;
using std::vector;

static int failed = 0;

static void ok (const string &what)
{
    printf("ok    %s\n", what.c_str());
}

static void fail (const string &what, const string &detail)
{
    printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
           detail.c_str());
    failed++;
}

static void okOrFail (bool good, const string &what, const string &detail)
{
    if (good)
        ok(what);
    else
        fail(what, detail);
}

static string num (double v)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%.6g", v);

    return buf;
}

/* ---- building a graph without a .dsp ------------------------------------
 *
 * hostcheck's shape, and for its reason: these are inputs the corpus has no
 * way to hold.
 */

struct Wire   { const char *arg, *fromNode, *fromArg; };
struct Value  { const char *arg; float value; };
/* The one arg in the tree that is not a number: osc::sample's `file'. */
struct Text   { const char *arg; const char *value; };

struct NodeSpec
{
    const char *name;
    const char *spelling;              /* "filt/svf" */
    vector<Value> values;
    vector<Text>  texts;
    vector<Wire>  wires;
};

struct Watch { const char *node, *arg; };

static bool
buildGraph (thSynth &synth, thSynthTree &tree, const vector<NodeSpec> &spec,
            string &why)
{
    for (size_t i = 0; i < spec.size(); i++)
    {
        thPlugin *p =
            synth.getPluginManager()->getOrLoadPlugin(spec[i].spelling);

        if (p == NULL || p->state() == thPlugin::NOTLOADED)
        {
            why = string("could not load ") + spec[i].spelling;
            return false;
        }

        tree.newNode(new thNode(spec[i].name, p), true);
    }

    for (size_t i = 0; i < spec.size(); i++)
    {
        thNode *n = tree.findNode(spec[i].name);

        for (size_t v = 0; v < spec[i].values.size(); v++)
            n->setArg(spec[i].values[v].arg, spec[i].values[v].value);

        for (size_t t = 0; t < spec[i].texts.size(); t++)
            n->setTextArg(spec[i].texts[t].arg, spec[i].texts[t].value);

        for (size_t w = 0; w < spec[i].wires.size(); w++)
            n->setArg(spec[i].wires[w].arg, spec[i].wires[w].fromNode,
                      spec[i].wires[w].fromArg);
    }

    /* The synthetic io node thcNodeHost invents, for the same reason:
       process() fires what it can reach from there, and every node in these
       graphs is reachable from its first output. */
    thNode *io = new thNode("ionode", NULL);

    tree.newNode(io, true);

    for (size_t i = 0; i < spec.size(); i++)
    {
        thPlugin *p =
            synth.getPluginManager()->getOrLoadPlugin(spec[i].spelling);
        string outArg;

        for (int a = 0; p != NULL && a < p->argCount(); a++)
            if (p->getArgDir(a) == thPlugin::ARG_OUT)
            {
                outArg = p->getArgName(a);
                break;
            }

        if (!outArg.empty())
            io->setArg(spec[i].name, spec[i].name, outArg);
    }

    tree.setIONode("ionode");
    tree.buildArgMap();
    tree.setPointers();
    tree.buildSynthTree();

    return true;
}

/* `samples' samples of each watched arg, cut into windows of `windowlen'.
 *
 * A fresh synth per call: two renders that shared one would share every
 * plugin's per-synth state, and osc::noise's whole determinism claim is
 * about what a fresh one does. */
static bool
render (const string &pluginPath, const vector<NodeSpec> &spec,
        const vector<Watch> &watch, unsigned windowlen, unsigned samples,
        vector< vector<float> > &out, string &why)
{
    thSynth synth(pluginPath, (int)windowlen, TH_DEFAULT_SAMPLES);
    thSynthTree tree("statecheck", &synth);

    if (!buildGraph(synth, tree, spec, why))
        return false;

    vector<thArg *> args;

    for (size_t w = 0; w < watch.size(); w++)
    {
        thNode *n = tree.findNode(watch[w].node);
        thArg *a = n ? n->getArg(watch[w].arg) : NULL;

        if (a == NULL)
        {
            why = string(watch[w].node) + " has no arg '" + watch[w].arg + "'";
            return false;
        }

        args.push_back(a);
    }

    out.assign(watch.size(), vector<float>());

    for (unsigned done = 0; done < samples; done += windowlen)
    {
        tree.setActiveNodes();
        tree.process(windowlen);

        for (unsigned i = 0; i < windowlen && done + i < samples; i++)
            for (size_t w = 0; w < args.size(); w++)
                out[w].push_back((*args[w])[i]);
    }

    return true;
}

/* One watched arg, which is what most of the checks below want. */
static bool
render1 (const string &pluginPath, const vector<NodeSpec> &spec,
         const char *node, const char *arg, unsigned windowlen,
         unsigned samples, vector<float> &out, string &why)
{
    vector<Watch> watch;
    vector< vector<float> > got;
    Watch w = { node, arg };

    watch.push_back(w);

    if (!render(pluginPath, spec, watch, windowlen, samples, got, why))
        return false;

    out = got[0];

    return true;
}

/* The same graph at two window lengths, bitwise. A stateful plugin that
   keeps anything in a local, or reads buf[i - 1] without remembering
   buf[len - 1], differs here and nowhere else. */
static void
windowsAgree (const string &pluginPath, const vector<NodeSpec> &spec,
              const char *node, const char *arg, const string &what)
{
    vector<float> one, many;
    string why;

    if (!render1(pluginPath, spec, node, arg, 1, 2000, one, why) ||
        !render1(pluginPath, spec, node, arg, 500, 2000, many, why))
    {
        fail(what, why);
        return;
    }

    for (size_t i = 0; i < one.size() && i < many.size(); i++)
        if (memcmp(&one[i], &many[i], sizeof(float)) != 0)
        {
            fail(what, "sample " + num((double)i) + ": " + num(one[i]) +
                       " one at a time, " + num(many[i]) + " five hundred");
            return;
        }

    ok(what);
}

static double rms (const vector<float> &v, size_t from)
{
    double sum = 0;
    size_t n = 0;

    for (size_t i = from; i < v.size(); i++, n++)
        sum += (double)v[i] * v[i];

    return n ? sqrt(sum / n) : 0;
}

/* The largest excursion, which for a settled sine is its amplitude. */
static double peak (const vector<float> &v, size_t from)
{
    double top = 0;

    for (size_t i = from; i < v.size(); i++)
        if (fabs(v[i]) > top)
            top = fabs(v[i]);

    return top;
}

static bool allFinite (const vector<float> &v)
{
    for (size_t i = 0; i < v.size(); i++)
        if (!thIsFinite(v[i]))
            return false;

    return true;
}

/* ---- misc::slew --------------------------------------------------------- */

/* A square wave into the lag: one edge, of a known size, at a sample the
   harness finds rather than assumes. A constant would not do -- the lag
   starts where its input is, so a signal that never moves is a signal it
   never lags -- and that is itself one of the things checked below. */
static vector<NodeSpec> slewGraph (float time, float hz)
{
    vector<NodeSpec> spec;
    NodeSpec src, lag;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 2 };           /* square */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    lag.name = "lag";
    lag.spelling = "misc/slew";

    Value t = { "time", time };
    Wire  in = { "in", "src", "out" };

    lag.values.push_back(t);
    lag.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(lag);

    return spec;
}

/* The first sample at which the square changed, which is the edge the
   response below is measured from. */
static size_t firstEdge (const vector<float> &v)
{
    for (size_t i = 1; i < v.size(); i++)
        if (v[i] != v[i - 1])
            return i;

    return 0;
}

static void checkSlew (const string &pluginPath)
{
    /* 1 - 1/e and 1 - 1/e^4. What `time' means. */
    static const double AT_ONE  = 0.6321205588;
    static const double AT_FOUR = 0.9816843611;

    static const float times[] = { 32, 441, 4410 };

    int checked = 0;
    bool bad = false;
    bool started = true;
    string startDetail;

    for (size_t c = 0; c < sizeof(times) / sizeof(times[0]); c++)
    {
        const float t = times[c];
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "lag", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        /* One hertz: the edge lands halfway through, which leaves room for
           four of the longest time constant before the square flips back. */
        if (!render(pluginPath, slewGraph(t, 1), watch, 256, 40000, got, why))
        {
            fail("misc::slew renders", why);
            bad = true;
            break;
        }

        const size_t e = firstEdge(got[0]);

        if (e == 0)
        {
            fail("the square this is measured against has an edge in it", "");
            bad = true;
            break;
        }

        /* Before the edge the lag has nothing to lag: it started where its
           input was and the input has not moved. */
        for (size_t i = 0; i < e; i++)
            if (got[1][i] != got[0][i])
            {
                started = false;
                startDetail = "sample " + num((double)i) + ": input " +
                              num(got[0][i]) + ", lag " + num(got[1][i]);
                break;
            }

        const double from = got[0][e - 1], to = got[0][e];
        const double one = (got[1][e + (size_t)t - 1] - from) / (to - from);
        const double four =
            (got[1][e + (size_t)(t * 4) - 1] - from) / (to - from);

        /* A tenth of a percent: the discrete sum is the exponential exactly,
           so what is left is float rounding over four thousand steps. */
        if (fabs(one - AT_ONE) > 0.001 || fabs(four - AT_FOUR) > 0.001)
        {
            fail("a step reaches 63% at `time' and 98% at four of them",
                 "time " + num(t) + ": " + num(one) + " and " + num(four));
            bad = true;
            break;
        }

        checked++;
    }

    if (!bad)
    {
        ok("misc::slew: a step is 63% covered at `time', 98% at four of "
           "them (" + num(checked) + " time constants)");

        okOrFail(started, "misc::slew: a voice starts where its input is, "
                          "not at zero", startDetail);
    }

    /* A lag of less than a sample is a wire. `time = 0' is the one a graph
       writes; the negative and the fraction are what a knob or a node driving
       it can produce. */
    {
        static const float nothing[] = { 0, 0.5f, -100 };

        bool same = true;
        string detail;

        for (size_t c = 0; c < sizeof(nothing) / sizeof(nothing[0]); c++)
        {
            vector<Watch> watch;
            vector< vector<float> > got;
            string why;

            Watch w0 = { "src", "out" };
            Watch w1 = { "lag", "out" };

            watch.push_back(w0);
            watch.push_back(w1);

            if (!render(pluginPath, slewGraph(nothing[c], 400), watch, 64,
                        4096, got, why))
            {
                same = false;
                detail = why;
                break;
            }

            for (size_t i = 0; i < got[0].size(); i++)
                if (got[1][i] != got[0][i])
                {
                    same = false;
                    detail = "time " + num(nothing[c]) + ", sample " +
                             num((double)i) + ": input " + num(got[0][i]) +
                             ", lag " + num(got[1][i]);
                    break;
                }
        }

        okOrFail(same, "misc::slew: a `time' under one sample passes the "
                       "input straight through", detail);
    }

    windowsAgree(pluginPath, slewGraph(441, 40), "lag", "out",
                 "misc::slew: the same lag at one sample a window and at "
                 "five hundred");
}

/* ---- filt::svf ---------------------------------------------------------- */

/* A source into the filter. `freq' of 0 with a square wave is the closest
   thing to a constant the oscillators offer, so DC comes from osc::simple at
   a frequency the wavelength floor turns into one. */
static vector<NodeSpec> svfGraph (float freq, float amp, float cutoff,
                                  float res, int waveform)
{
    vector<NodeSpec> spec;
    NodeSpec src, filt;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", freq };
    Value a = { "amp", amp };
    Value w = { "waveform", (float)waveform };

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    filt.name = "filt";
    filt.spelling = "filt/svf";

    Value c = { "cutoff", cutoff };
    Value r = { "res", res };
    Wire  in = { "in", "src", "out" };

    filt.values.push_back(c);
    filt.values.push_back(r);
    filt.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(filt);

    return spec;
}

static void checkSvf (const string &pluginPath)
{
    /* ---- the three outputs sum to the input ---- */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        /* The source's output rather than the filter's `in': an arg that is
           a wire carries no samples of its own, so reading one from outside
           the callback gets the zero it was allocated with. Same buffer
           either way -- it is the one the callback dereferences. */
        Watch w0 = { "src", "out" };
        Watch w1 = { "filt", "out_low" };
        Watch w2 = { "filt", "out_band" };
        Watch w3 = { "filt", "out_high" };

        watch.push_back(w0);
        watch.push_back(w1);
        watch.push_back(w2);
        watch.push_back(w3);

        /* A sawtooth, so the sum has every frequency in it, and a quarter
           scale, so nothing reaches the output clip: the step at the end of
           each cycle arrives at out_high whole, on top of whatever the other
           two are carrying, which at half scale is enough to clip and the
           identity is then a statement about the clipper. */
        if (!render(pluginPath, svfGraph(300, 0.25f, 900, 0.7f, 1), watch, 256,
                    4096, got, why))
            fail("filt::svf renders", why);
        else
        {
            double worst = 0;

            for (size_t i = 0; i < got[0].size(); i++)
            {
                const double sum = (double)got[1][i] + got[2][i] + got[3][i];
                const double err = fabs(sum - got[0][i]);

                if (err > worst)
                    worst = err;
            }

            /* Single precision over a sum of three terms, each of order one:
               a few ulps. */
            okOrFail(worst < 1e-5,
                     "filt::svf: low + band + high is the input, sample for "
                     "sample", "worst error " + num(worst));
        }
    }

    /* ---- DC goes through the low output and not through the high one ---- */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "filt", "out_low" };
        Watch w1 = { "filt", "out_band" };
        Watch w2 = { "filt", "out_high" };

        watch.push_back(w0);
        watch.push_back(w1);
        watch.push_back(w2);

        /* A square at the wavelength floor is a constant for as long as this
           renders: half a cycle is eight million samples. */
        if (!render(pluginPath, svfGraph(0, 0.5f, 1000, 0.3f, 2), watch, 256,
                    8192, got, why))
            fail("filt::svf renders DC", why);
        else
        {
            /* Settled: four thousand samples is many time constants at a
               cutoff of a thousand hertz. */
            const double low = rms(got[0], 4096);
            const double band = rms(got[1], 4096);
            const double high = rms(got[2], 4096);
            const double in = 0.5;

            okOrFail(fabs(low - in) < 1e-3 && band < 1e-3 && high < 1e-3,
                     "filt::svf: a constant comes out of out_low unchanged "
                     "and out of the other two not at all",
                     "low " + num(low) + ", band " + num(band) + ", high " +
                     num(high));
        }
    }

    /* ---- the band output peaks at 1 at the cutoff, whatever the res ---- */
    {
        static const float resonances[] = { 0, 0.5f, 0.9f, 0.99f };
        static const float cutoffs[] = { 100, 1000, 5000 };

        double worst = 0;
        string detail;
        bool bad = false;

        for (size_t r = 0; r < sizeof(resonances) / sizeof(resonances[0]); r++)
            for (size_t c = 0; c < sizeof(cutoffs) / sizeof(cutoffs[0]); c++)
            {
                vector<float> out;
                string why;

                /* Long enough for the slowest of these to settle: a Q of 50
                   at a hundred hertz rings for about a second. */
                const unsigned samples = 200000;

                if (!render1(pluginPath,
                             svfGraph(cutoffs[c], 0.5f, cutoffs[c],
                                      resonances[r], 0),
                             "filt", "out_band", 512, samples, out, why))
                {
                    fail("filt::svf renders a sine at its cutoff", why);
                    bad = true;
                    break;
                }

                const double got = peak(out, samples * 3 / 4) / 0.5;

                if (fabs(got - 1.0) > worst)
                {
                    worst = fabs(got - 1.0);
                    detail = "cutoff " + num(cutoffs[c]) + ", res " +
                             num(resonances[r]) + ": " + num(got);
                }
            }

        if (!bad)
            /* One percent, and what is in it is the peak of a sampled sine
               falling between two samples rather than on one: the filter's
               own answer is exact. It was three percent when this filter
               oversampled by two, which is the measurement that stopped it
               oversampling -- see the head of plugins/filt/svf.cpp. */
            okOrFail(worst < 0.01,
                     "filt::svf: a sine at the cutoff leaves out_band at the "
                     "level it arrived, at every resonance", detail);
    }

    /* ---- resonance is resonance ---- */
    {
        double flat = 0, sharp = 0;
        vector<float> out;
        string why;

        /* A tenth of full scale: a Q of 5 on half of it is two and a half,
           and what would be measured then is the output clip. */
        if (!render1(pluginPath, svfGraph(1000, 0.1f, 1000, 0, 0), "filt",
                     "out_low", 512, 100000, out, why))
            fail("filt::svf renders", why);
        else
        {
            flat = peak(out, 75000);

            if (!render1(pluginPath, svfGraph(1000, 0.1f, 1000, 0.9f, 0),
                         "filt", "out_low", 512, 100000, out, why))
                fail("filt::svf renders", why);
            else
            {
                sharp = peak(out, 75000);

                /* res 0 is a damping of 2, so a Q of 0.5 and half the input
                   at the cutoff; res 0.9 is a Q of 5. */
                okOrFail(fabs(flat / 0.1 - 0.5) < 0.02 &&
                         fabs(sharp / 0.1 - 5.0) < 0.15,
                         "filt::svf: the low output at the cutoff is the Q, "
                         "and the Q is what `res' says",
                         "res 0: " + num(flat / 0.1) + ", res 0.9: " +
                         num(sharp / 0.1));
            }
        }
    }

    /* ---- and nothing anywhere in the plane goes non-finite ---- */
    {
        /* Past Nyquist and below zero on the cutoff, past the top and below
           the bottom on the resonance: the clamps are part of what is being
           checked, since a graph may drive either from a node. */
        static const float cutoffs[] = {
            -100, 0, 1, 20, 1000, 11025, 22050, 44100, 1e9f
        };
        static const float resonances[] = { -1, 0, 0.5f, 0.99f, 1, 4 };

        int cases = 0;
        bool bad = false;

        for (size_t c = 0; c < sizeof(cutoffs) / sizeof(cutoffs[0]) && !bad;
             c++)
            for (size_t r = 0;
                 r < sizeof(resonances) / sizeof(resonances[0]) && !bad; r++)
            {
                vector<Watch> watch;
                vector< vector<float> > got;
                string why;

                Watch w0 = { "filt", "out_low" };
                Watch w1 = { "filt", "out_band" };
                Watch w2 = { "filt", "out_high" };

                watch.push_back(w0);
                watch.push_back(w1);
                watch.push_back(w2);

                /* A full-scale saw at the cutoff is the worst case: every
                   harmonic, and the fundamental sitting on the resonance. */
                if (!render(pluginPath,
                            svfGraph(cutoffs[c] > 20 ? cutoffs[c] : 20,
                                     TH_MAX, cutoffs[c], resonances[r], 1),
                            watch, 512, 40000, got, why))
                {
                    fail("filt::svf renders at a corner", why);
                    bad = true;
                    break;
                }

                for (size_t w = 0; w < got.size(); w++)
                    if (!allFinite(got[w]))
                    {
                        fail("filt::svf stays finite at every corner",
                             "cutoff " + num(cutoffs[c]) + ", res " +
                             num(resonances[r]));
                        bad = true;
                        break;
                    }

                cases++;
            }

        if (!bad)
            ok("filt::svf: " + num(cases) + " corners of cutoff against "
               "resonance, every sample a number");
    }

    windowsAgree(pluginPath, svfGraph(300, 0.5f, 900, 0.9f, 1), "filt",
                 "out_low",
                 "filt::svf: the same filter at one sample a window and at "
                 "five hundred");
}

/* ---- osc::noise --------------------------------------------------------- */

static vector<NodeSpec> noiseGraph (float color, float amp)
{
    vector<NodeSpec> spec;
    NodeSpec n;

    n.name = "noise";
    n.spelling = "osc/noise";

    Value c = { "color", color };
    Value a = { "amp", amp };

    n.values.push_back(c);
    n.values.push_back(a);

    spec.push_back(n);

    return spec;
}

/* The average size of the step from one sample to the next, against the
   signal's own size. A first difference is a high-pass with a 6 dB an octave
   slope, so this is the spectral tilt with no FFT in it: white sits at
   sqrt(2), and every octave of darkening drops it. */
static double tilt (const vector<float> &v)
{
    vector<float> d;

    for (size_t i = 1; i < v.size(); i++)
        d.push_back(v[i] - v[i - 1]);

    const double level = rms(v, 0);

    return level > 0 ? rms(d, 0) / level : 0;
}

static void checkNoise (const string &pluginPath)
{
    static const char *const names[] = { "white", "pink", "brown" };

    vector<float> got[3];

    /* ---- two fresh synths, the same noise ---- */
    {
        bool same = true;
        string detail;

        for (int c = 0; c < 3 && same; c++)
        {
            vector<float> again;
            string why;

            if (!render1(pluginPath, noiseGraph((float)c, 0.5f), "noise",
                         "out", 256, 40000, got[c], why) ||
                !render1(pluginPath, noiseGraph((float)c, 0.5f), "noise",
                         "out", 256, 40000, again, why))
            {
                fail("osc::noise renders", why);
                return;
            }

            for (size_t i = 0; i < got[c].size(); i++)
                if (memcmp(&got[c][i], &again[i], sizeof(float)) != 0)
                {
                    same = false;
                    detail = string(names[c]) + " differs at sample " +
                             num((double)i);
                    break;
                }
        }

        okOrFail(same, "osc::noise: two fresh synths draw the same noise, "
                       "bit for bit, in all three colors", detail);
    }

    /* ---- and it stays inside `amp' ---- */
    {
        bool inside = true;
        string detail;

        for (int c = 0; c < 3; c++)
            if (peak(got[c], 0) > 0.5)
            {
                inside = false;
                detail = string(names[c]) + " reaches " +
                         num(peak(got[c], 0));
                break;
            }

        okOrFail(inside, "osc::noise: no color leaves `amp'", detail);
    }

    /* ---- white, then pink, then brown ---- */
    {
        const double w = tilt(got[0]), p = tilt(got[1]), b = tilt(got[2]);

        /* White's first difference is two independent draws, so its tilt is
           sqrt(2) exactly; the two filters take it down from there, and what
           is checked is the order and that each step is a real one rather
           than the exact figures, which are the coefficients' business. */
        okOrFail(fabs(w - sqrt(2.0)) < 0.01 && p < w * 0.75 && b < p * 0.25,
                 "osc::noise: white is flat, pink is darker, brown is darker "
                 "again", "white " + num(w) + ", pink " + num(p) +
                          ", brown " + num(b));
    }

    /* ---- and each color is at the level the plugin says ---- */
    {
        /* White is uniform over -amp..amp, so its RMS is amp/sqrt(3); pink
           and brown put five standard deviations at amp, so theirs is
           amp/5. Pinning these is what makes the scaling constants in the
           plugin a claim rather than a taste.
         *
           Two million samples, against forty thousand for everything else
           above: brown noise is mostly under fifty hertz, so a second of it
           holds a few dozen cycles of what it is made of and an RMS over
           that is several percent away from the true one on any given run.
           A minute of it settles to half of one. */
        const double want[3] = { 0.5 / sqrt(3.0), 0.5 / 5.0, 0.5 / 5.0 };

        bool good = true;
        string detail;

        for (int c = 0; c < 3; c++)
        {
            vector<float> run;
            string why;
            double level;

            if (!render1(pluginPath, noiseGraph((float)c, 0.5f), "noise",
                         "out", 1024, 2000000, run, why))
            {
                fail("osc::noise renders", why);
                return;
            }

            level = rms(run, 0);

            if (fabs(level - want[c]) > want[c] * 0.02)
            {
                good = false;
                detail = string(names[c]) + " is " + num(level) + ", not " +
                         num(want[c]);
                break;
            }
        }

        okOrFail(good, "osc::noise: white fills `amp' and pink and brown sit "
                       "five sigma inside it", detail);
    }

    /* ---- `color' is a selector, and `amp' of 0 is full scale ---- */
    {
        vector<float> pinkish, full;
        string why;

        if (!render1(pluginPath, noiseGraph(1.9f, 0.5f), "noise", "out", 256,
                     4000, pinkish, why) ||
            !render1(pluginPath, noiseGraph(0, 0), "noise", "out", 256, 4000,
                     full, why))
            fail("osc::noise renders", why);
        else
        {
            bool selector = true;

            for (size_t i = 0; i < pinkish.size(); i++)
                if (memcmp(&pinkish[i], &got[1][i], sizeof(float)) != 0)
                {
                    selector = false;
                    break;
                }

            okOrFail(selector && fabs(rms(full, 0) - TH_MAX / sqrt(3.0)) <
                                     TH_MAX * 0.02,
                     "osc::noise: 1.9 is pink, and an `amp' of 0 is full "
                     "scale", "");
        }
    }

    windowsAgree(pluginPath, noiseGraph(1, 0.5f), "noise", "out",
                 "osc::noise: the same noise at one sample a window and at "
                 "five hundred");
}

/* ---- misc::vibrato ------------------------------------------------------ */

/* A frequency that does not move, into the thing that moves it. That is
   every use of this node: `in' is what the note is and `out' is what the
   note does, and neither wants a source with a shape of its own. */
static vector<NodeSpec> vibratoGraph (float in, float rate, float depth,
                                      float delay, float rise)
{
    vector<NodeSpec> spec;
    NodeSpec vib;

    vib.name = "vib";
    vib.spelling = "misc/vibrato";

    Value i = { "in", in };
    Value r = { "rate", rate };
    Value d = { "depth", depth };
    Value l = { "delay", delay };
    Value g = { "rise", rise };

    vib.values.push_back(i);
    vib.values.push_back(r);
    vib.values.push_back(d);
    vib.values.push_back(l);
    vib.values.push_back(g);

    spec.push_back(vib);

    return spec;
}

/* The same bend, on the frequency of a sine, which is what a graph wires
   it to. What the oscillator does with it is the end-to-end claim: a
   wobble in `out' that no oscillator ever read would measure just as
   well and mean nothing. */
static vector<NodeSpec> vibratoSineGraph (float in, float rate, float depth)
{
    vector<NodeSpec> spec = vibratoGraph(in, rate, depth, 0, 0);
    NodeSpec osc;

    osc.name = "osc";
    osc.spelling = "osc/simple";

    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 0 };            /* sine */
    Wire  f = { "freq", "vib", "out" };

    osc.values.push_back(a);
    osc.values.push_back(w);
    osc.wires.push_back(f);

    spec.push_back(osc);

    return spec;
}

/* Where a rising signal crosses zero, to a fraction of a sample. The
   spacing of these is an instantaneous frequency, which is the only way
   to ask an oscillator what note it is playing. */
static vector<double> upCrossings (const vector<float> &v)
{
    vector<double> out;

    for (size_t i = 1; i < v.size(); i++)
        if (v[i - 1] <= 0 && v[i] > 0)
            out.push_back((double)i - 1 +
                          (double)(-v[i - 1]) / (v[i] - v[i - 1]));

    return out;
}

static void checkVibrato (const string &pluginPath)
{
    const double rate = 5, depth = 100, carrier = 440;
    const double cycle = TH_DEFAULT_SAMPLES / rate;
    const double up = pow(2.0, depth / 1200.0);

    /* ---- the bend is `depth' cents, either way ---- */

    /* Four LFO cycles, so the extremes are reached whatever the phase
       does at the start. A cent is a thousandth of a semitone and the
       arithmetic is one exp2 in float: a thousandth of the bend is a
       loose tolerance for it and a tight one for anything else. */
    {
        vector<float> out;
        string why;

        if (!render1(pluginPath, vibratoGraph(carrier, rate, depth, 0, 0),
                     "vib", "out", 256, (unsigned)(cycle * 4), out, why))
            fail("misc::vibrato renders", why);
        else
        {
            double top = 0, bottom = out.empty() ? 0 : out[0];

            for (size_t i = 0; i < out.size(); i++)
            {
                if (out[i] > top)
                    top = out[i];

                if (out[i] < bottom)
                    bottom = out[i];
            }

            okOrFail(fabs(top / carrier - up) < 0.001 &&
                     fabs(bottom / carrier - 1 / up) < 0.001,
                     "misc::vibrato: the bend reaches `depth' cents either "
                     "way",
                     "wanted " + num(carrier * up) + " and " +
                     num(carrier / up) + ", got " + num(top) + " and " +
                     num(bottom));
        }
    }

    /* ---- and it goes round at `rate' ---- */

    {
        vector<float> out;
        string why;

        if (!render1(pluginPath, vibratoGraph(carrier, rate, depth, 0, 0),
                     "vib", "out", 256, (unsigned)(cycle * 6), out, why))
            fail("misc::vibrato renders", why);
        else
        {
            /* The bend either side of the note, whose crossings are the
               LFO's own -- `out' itself never reaches zero. */
            vector<float> bend;

            for (size_t i = 0; i < out.size(); i++)
                bend.push_back((float)(out[i] - carrier));

            const vector<double> at = upCrossings(bend);
            bool even = at.size() >= 5;
            string detail = "saw " + num((double)at.size()) + " cycles";

            for (size_t i = 1; i < at.size() && even; i++)
                if (fabs(at[i] - at[i - 1] - cycle) > 1)
                {
                    even = false;
                    detail = "cycle " + num((double)i) + " was " +
                             num(at[i] - at[i - 1]) + " samples, wanted " +
                             num(cycle);
                }

            okOrFail(even, "misc::vibrato: one cycle of the bend every "
                           "1/`rate' seconds", detail);
        }
    }

    /* ---- cents, not hertz ---- */

    /* The whole reason the arithmetic is an exponent. Two notes two
       octaves apart bend by the same *interval*, which means the two
       outputs divided by their own inputs are one signal. A vibrato in
       hertz passes every other check on this list and fails this one. */
    {
        vector<float> low, high;
        string why;

        if (!render1(pluginPath, vibratoGraph(110, rate, depth, 0, 0),
                     "vib", "out", 256, (unsigned)cycle, low, why) ||
            !render1(pluginPath, vibratoGraph(440, rate, depth, 0, 0),
                     "vib", "out", 256, (unsigned)cycle, high, why))
            fail("misc::vibrato renders", why);
        else
        {
            bool same = true;
            string detail;

            for (size_t i = 0; i < low.size() && i < high.size(); i++)
                if (fabs(low[i] / 110.0 - high[i] / 440.0) > 1e-6)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(low[i] / 110.0) + " against " +
                             num(high[i] / 440.0);
                    break;
                }

            okOrFail(same, "misc::vibrato: the same interval at every "
                           "pitch, which is what cents buy", detail);
        }
    }

    /* ---- the note is held straight for `delay' ---- */

    {
        const double delay = TH_DEFAULT_SAMPLES / 2;
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     vibratoGraph(carrier, rate, depth, (float)delay, 0),
                     "vib", "out", 256,
                     (unsigned)(delay + cycle), out, why))
            fail("misc::vibrato renders", why);
        else
        {
            bool straight = true, moved = false;
            string detail;

            for (size_t i = 0; i < out.size(); i++)
            {
                if (i <= (size_t)delay && out[i] != (float)carrier)
                {
                    straight = false;
                    detail = "sample " + num((double)i) + " of " +
                             num(delay) + " was already " + num(out[i]);
                    break;
                }

                if (i > (size_t)delay && fabs(out[i] - carrier) > 1)
                    moved = true;
            }

            okOrFail(straight && moved,
                     "misc::vibrato: nothing bends until `delay' is up, and "
                     "then it does", detail);
        }
    }

    /* ---- and grows into it over `rise' ---- */

    /* Cycle by cycle: each one has to reach further than the one before
       it, and the ones after the ramp has finished have to reach the
       whole way. A ramp that jumped, or one that never arrived, is a
       different sequence of peaks from this one. */
    {
        const double rise = cycle * 2;               /* two LFO cycles */
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     vibratoGraph(carrier, rate, depth, 0, (float)rise),
                     "vib", "out", 256, (unsigned)(cycle * 8), out, why))
            fail("misc::vibrato renders", why);
        else
        {
            vector<double> reach;

            for (size_t c = 0; c * cycle < out.size(); c++)
            {
                double top = 0;

                for (size_t i = (size_t)(c * cycle);
                     i < out.size() && i < (size_t)((c + 1) * cycle); i++)
                    if (out[i] - carrier > top)
                        top = out[i] - carrier;

                reach.push_back(top);
            }

            bool growing = reach.size() >= 6;
            string detail;

            for (size_t c = 1; c < reach.size() && c < 3 && growing; c++)
                if (reach[c] <= reach[c - 1])
                {
                    growing = false;
                    detail = "cycle " + num((double)c) + " reached " +
                             num(reach[c]) + " after " + num(reach[c - 1]);
                }

            /* Two cycles of ramp, so by the fourth the bend is whole. */
            for (size_t c = 3; c < reach.size() && growing; c++)
                if (fabs(reach[c] - carrier * (up - 1)) > 0.5)
                {
                    growing = false;
                    detail = "cycle " + num((double)c) + " reached " +
                             num(reach[c]) + ", wanted " +
                             num(carrier * (up - 1));
                }

            okOrFail(growing, "misc::vibrato: the bend grows over `rise' "
                              "and stays there afterwards", detail);
        }
    }

    /* ---- on a sine, which is what a graph wires it to ---- */

    /* The end-to-end claim: an oscillator reading this plays a note whose
       own frequency is `depth' cents either way. Measured off the sine's
       zero crossings, which is how you would measure a real one. */
    {
        const double wide = 200;
        vector<float> out;
        string why;

        if (!render1(pluginPath, vibratoSineGraph(220, rate, (float)wide),
                     "osc", "out", 256, (unsigned)(cycle * 4), out, why))
            fail("misc::vibrato on a sine renders", why);
        else
        {
            const vector<double> at = upCrossings(out);
            double fastest = 0, slowest = 1e9;

            for (size_t i = 1; i < at.size(); i++)
            {
                const double hz = TH_DEFAULT_SAMPLES / (at[i] - at[i - 1]);

                if (hz > fastest)
                    fastest = hz;

                if (hz < slowest)
                    slowest = hz;
            }

            /* One period at a time, so the measurement is of the average
               over that period rather than of the peak of the bend -- the
               LFO moves under it. A percent of the interval is what that
               costs at these numbers. */
            const double want = pow(2.0, wide / 1200.0);

            okOrFail(at.size() > 100 &&
                     fabs(fastest / 220.0 - want) < 0.01 &&
                     fabs(slowest / 220.0 - 1 / want) < 0.01,
                     "misc::vibrato: a sine reading it plays `depth' cents "
                     "either side of its note",
                     "wanted " + num(220 * want) + " and " +
                     num(220 / want) + ", measured " + num(fastest) +
                     " and " + num(slowest));
        }
    }

    windowsAgree(pluginPath, vibratoGraph(carrier, rate, depth, 1000, 1000),
                 "vib", "out",
                 "misc::vibrato: the same bend at one sample a window and "
                 "at five hundred");
}

/* ---- delay::allpass ----------------------------------------------------- */

/* A steady sine into the line, which is how you ask a filter what it
   does to a frequency. */
static vector<NodeSpec> allpassSineGraph (float hz, float delay, float gain)
{
    vector<NodeSpec> spec;
    NodeSpec src, ap;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 0 };            /* sine */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    ap.name = "ap";
    ap.spelling = "delay/allpass";

    Value d = { "delay", delay };
    Value g = { "gain", gain };
    Wire  in = { "in", "src", "out" };

    ap.values.push_back(d);
    ap.values.push_back(g);
    ap.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(ap);

    return spec;
}

/* And a burst into it, which is how you ask what it does to a room. An
   env::ad with no attack fires once at the top of the voice and is over
   in `d' samples, so what follows is the line's own answer. */
static vector<NodeSpec> allpassBurstGraph (float delay, float gain)
{
    vector<NodeSpec> spec = allpassSineGraph(0, delay, gain);

    spec[0].spelling = "env/ad";
    spec[0].values.clear();

    Value a = { "a", 0 };
    Value d = { "d", 64 };
    Value p = { "p", TH_MAX };

    spec[0].values.push_back(a);
    spec[0].values.push_back(d);
    spec[0].values.push_back(p);

    return spec;
}

static double energy (const vector<float> &v, size_t from)
{
    double sum = 0;

    for (size_t i = from; i < v.size(); i++)
        sum += (double)v[i] * v[i];

    return sum;
}

static void checkAllpass (const string &pluginPath)
{
    const float delay = 137, gain = 0.7f;

    /* ---- every frequency comes out at the level it went in ---- */

    /* Which is the whole name of the thing. RMS rather than peak,
       because the two signals are the same sine at different phases and
       a sampled peak depends on where the samples fall in the cycle --
       a quarter of a percent at these frequencies, which is the size of
       the answer. Over a settled second, whole cycles either way. */
    {
        static const float hz[] = { 110, 440, 1000, 5000, 11025 };

        bool flat = true;
        string detail;

        for (size_t c = 0; c < sizeof(hz) / sizeof(hz[0]) && flat; c++)
        {
            vector<Watch> watch;
            vector< vector<float> > got;
            string why;

            Watch w0 = { "src", "out" };
            Watch w1 = { "ap", "out" };

            watch.push_back(w0);
            watch.push_back(w1);

            if (!render(pluginPath, allpassSineGraph(hz[c], delay, gain),
                        watch, 256, 44100, got, why))
            {
                fail("delay::allpass renders", why);
                return;
            }

            const double in = rms(got[0], 4410), out = rms(got[1], 4410);

            if (fabs(out / in - 1) > 0.01)
            {
                flat = false;
                detail = num(hz[c]) + " Hz came out at " +
                         num(out / in) + " of the level it went in at";
            }
        }

        okOrFail(flat, "delay::allpass: every frequency comes out at the "
                       "level it went in at", detail);
    }

    /* ---- and it is not a wire ---- */

    /* Flat on its own would be satisfied by a plugin that returned its
       input. What an allpass does is move it in time, so the output has
       to be a different signal with the same amplitude -- and with a
       delay of a third of a cycle, a very different one. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ap", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, allpassSineGraph(110, delay, gain), watch,
                    256, 20000, got, why))
            fail("delay::allpass renders", why);
        else
        {
            double apart = 0;

            for (size_t i = 4410; i < got[0].size(); i++)
                if (fabs(got[0][i] - got[1][i]) > apart)
                    apart = fabs(got[0][i] - got[1][i]);

            okOrFail(apart > TH_MAX * 0.5,
                     "delay::allpass: the output is the input moved in "
                     "time, not the input",
                     "furthest apart they got was " + num(apart));
        }
    }

    /* ---- a burst comes out with the energy it went in with ---- */

    /* Parseval, in the time domain and on the tape: a filter with a flat
       magnitude response neither adds energy nor loses it, so summing
       the squares either side is a check on the arithmetic that needs no
       spectrum. The tail has to have run out first -- 0.7 to the
       hundredth is nothing -- or this measures the render length. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ap", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, allpassBurstGraph(delay, gain), watch, 256,
                    40000, got, why))
            fail("delay::allpass renders", why);
        else
        {
            const double in = energy(got[0], 0), out = energy(got[1], 0);

            okOrFail(in > 0 && fabs(out / in - 1) < 0.001,
                     "delay::allpass: a burst comes out with the energy it "
                     "went in with",
                     "energy out over energy in was " + num(out / in));

            /* And it comes out as a run of echoes a `delay' apart, each
               quieter than the last. The first block holds the input's
               own inverted copy, so the comparison starts at the
               second. */
            vector<double> block;

            for (size_t b = 0; (b + 1) * (size_t)delay < got[1].size(); b++)
                block.push_back(peak(vector<float>(
                    got[1].begin() + (size_t)(b * delay),
                    got[1].begin() + (size_t)((b + 1) * delay)), 0));

            bool decays = block.size() > 20;
            string detail = "saw " + num((double)block.size()) + " blocks";

            for (size_t b = 2; b < 20 && b < block.size() && decays; b++)
                if (!(block[b] < block[b - 1]))
                {
                    decays = false;
                    detail = "echo " + num((double)b) + " was " +
                             num(block[b]) + " after " + num(block[b - 1]);
                }

            okOrFail(decays, "delay::allpass: the echoes are `delay' apart "
                             "and each is quieter than the last", detail);
        }
    }

    /* ---- and `gain = 0' is a plain delay ---- */

    /* The identity a graph reaches for when it wants the line and not the
       allpass, and the one case where the output can be named exactly:
       the input, `delay' samples ago, to the bit. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ap", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, allpassSineGraph(440, delay, 0), watch, 256,
                    20000, got, why))
            fail("delay::allpass renders", why);
        else
        {
            bool same = true;
            string detail;

            for (size_t i = (size_t)delay; i < got[0].size() && same; i++)
                if (memcmp(&got[1][i], &got[0][i - (size_t)delay],
                           sizeof(float)) != 0)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(got[1][i]) + " against " +
                             num(got[0][i - (size_t)delay]);
                }

            okOrFail(same, "delay::allpass: `gain = 0' is a plain delay of "
                           "`delay' samples", detail);
        }
    }

    windowsAgree(pluginPath, allpassSineGraph(440, delay, gain), "ap", "out",
                 "delay::allpass: the same tail at one sample a window and "
                 "at five hundred");
}

/* ---- delay::chorus ------------------------------------------------------ */

static vector<NodeSpec> chorusGraph (float hz, float rate, float depth,
                                     float delay, float mix, float taps)
{
    vector<NodeSpec> spec;
    NodeSpec src, ch;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 0 };            /* sine */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    ch.name = "ch";
    ch.spelling = "delay/chorus";

    Value r = { "rate", rate };
    Value dp = { "depth", depth };
    Value dl = { "delay", delay };
    Value mx = { "mix", mix };
    Value tp = { "taps", taps };
    Wire  in = { "in", "src", "out" };

    ch.values.push_back(r);
    ch.values.push_back(dp);
    ch.values.push_back(dl);
    ch.values.push_back(mx);
    ch.values.push_back(tp);
    ch.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(ch);

    return spec;
}

/* A click into one tap that does not move: the comb, with nothing else in
 * it. `mix = 1' so the output is the tap alone, `rate' and `depth' zero so
 * the tap sits exactly `delay' samples back, and one tap so the loop gain is
 * the knob rather than the knob over the number of readers.
 *
 * env::ad with no attack is the burst delay::allpass uses for the same job.
 */
static vector<NodeSpec> chorusClickGraph (float delay, float feedback)
{
    vector<NodeSpec> spec;
    NodeSpec src, ch;

    src.name = "src";
    src.spelling = "env/ad";

    Value a = { "a", 0 };
    Value d = { "d", 32 };
    Value p = { "p", TH_MAX };

    src.values.push_back(a);
    src.values.push_back(d);
    src.values.push_back(p);

    ch.name = "ch";
    ch.spelling = "delay/chorus";

    Value r = { "rate", 0 };
    Value dp = { "depth", 0 };
    Value dl = { "delay", delay };
    Value mx = { "mix", 1 };
    Value tp = { "taps", 1 };
    Value fb = { "feedback", feedback };
    Wire  in = { "in", "src", "out" };

    ch.values.push_back(r);
    ch.values.push_back(dp);
    ch.values.push_back(dl);
    ch.values.push_back(mx);
    ch.values.push_back(tp);
    ch.values.push_back(fb);
    ch.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(ch);

    return spec;
}

/* One bin of a DFT, by hand: the amplitude of the component at `hz' over
 * `n' samples. An FFT would want a window, a table and a power of two;
 * a single bin is two sums, and over a whole number of cycles of every
 * frequency asked about there is no leakage to window away. */
static double bin (const vector<float> &v, size_t from, size_t n, double hz)
{
    double re = 0, im = 0;

    for (size_t i = 0; i < n && from + i < v.size(); i++)
    {
        const double w = 2.0 * M_PI * hz * (double)i / TH_DEFAULT_SAMPLES;

        re += (double)v[from + i] * cos(w);
        im -= (double)v[from + i] * sin(w);
    }

    return 2.0 * sqrt(re * re + im * im) / (double)n;
}

static void checkChorus (const string &pluginPath)
{
    /* A whole second of a 441 Hz sine and a 3 Hz sweep: both are a whole
       number of cycles in the window, so every bin below lands on a bin
       center and nothing leaks into its neighbors. A depth of eight
       samples puts the modulation index at about a half, where the first
       sidebands are a quarter of the carrier and the second ones are a
       thirtieth -- big enough to measure and small enough that "the
       sidebands are at `rate'" is a statement about the first pair. */
    const double f0 = 441, rate = 3;
    const float depth = 8, delay = 220;
    const unsigned window = TH_DEFAULT_SAMPLES;
    const size_t from = TH_DEFAULT_SAMPLES / 4;

    /* ---- a sine comes out with sidebands a `rate' either side ---- */

    {
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     chorusGraph((float)f0, (float)rate, depth, delay, 1, 1),
                     "ch", "out", 256, window + (unsigned)from, out, why))
            fail("delay::chorus renders", why);
        else
        {
            const double carrier = bin(out, from, window, f0);
            const double lower = bin(out, from, window, f0 - rate);
            const double upper = bin(out, from, window, f0 + rate);
            const double second = bin(out, from, window, f0 + 2 * rate);

            okOrFail(carrier > 0 && lower / carrier > 0.15 &&
                     upper / carrier > 0.15 &&
                     lower / carrier < 0.40 && upper / carrier < 0.40 &&
                     upper > second * 3,
                     "delay::chorus: a sine comes out with sidebands a "
                     "`rate' either side of it",
                     "carrier " + num(carrier) + ", sidebands " +
                     num(lower) + " and " + num(upper) + ", second pair " +
                     num(second));
        }
    }

    /* ---- and with none at all when nothing moves ---- */

    /* The control for the check above: the same graph with the tap held
       still is a plain delay, and a plain delay has no sidebands. Without
       this, a plugin that rang at 3 Hz for any reason would pass. */
    {
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     chorusGraph((float)f0, (float)rate, 0, delay, 1, 1),
                     "ch", "out", 256, window + (unsigned)from, out, why))
            fail("delay::chorus renders", why);
        else
        {
            const double carrier = bin(out, from, window, f0);
            const double upper = bin(out, from, window, f0 + rate);

            okOrFail(carrier > 0 && upper / carrier < 0.01,
                     "delay::chorus: `depth = 0' is a plain delay, with "
                     "nothing either side",
                     "sideband over carrier was " + num(upper / carrier));
        }
    }

    /* ---- two taps do not cancel each other ---- */

    /* The reason each tap sits `depth' further back than the last. Two
       mirrors of one center are as sharp as each other is flat at every
       instant, and summing them to one output takes the pitch shift
       away entirely -- a real effect with a real name, and not this one.
       Staggered, the second tap leaves the first one's sidebands
       standing. */
    {
        vector<float> one, more;
        string why;
        bool stands = true;
        string detail;

        if (!render1(pluginPath,
                     chorusGraph((float)f0, (float)rate, depth, delay, 1, 1),
                     "ch", "out", 256, window + (unsigned)from, one, why))
            fail("delay::chorus renders", why);
        else
        {
            const double alone = bin(one, from, window, f0 + rate) /
                                 bin(one, from, window, f0);

            for (int taps = 2; taps <= 3 && stands; taps++)
            {
                if (!render1(pluginPath,
                             chorusGraph((float)f0, (float)rate, depth,
                                         delay, 1, (float)taps),
                             "ch", "out", 256, window + (unsigned)from,
                             more, why))
                {
                    fail("delay::chorus renders", why);
                    return;
                }

                const double together = bin(more, from, window, f0 + rate) /
                                        bin(more, from, window, f0);

                if (!(together > alone * 0.5))
                {
                    stands = false;
                    detail = "one tap put " + num(alone) + " of the "
                             "carrier into the sideband, " +
                             num((double)taps) + " put " + num(together);
                }
            }

            okOrFail(stands, "delay::chorus: a second and a third tap add "
                             "to the first rather than cancelling it",
                     detail);
        }
    }

    /* ---- the two identities ---- */

    /* `mix = 0' is a wire: the dry signal is what a mix of nothing
       leaves, and a graph with the node in it and the knob down has to
       be the graph without it. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ch", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath,
                    chorusGraph((float)f0, (float)rate, depth, delay, 0, 3),
                    watch, 256, 20000, got, why))
            fail("delay::chorus renders", why);
        else
        {
            bool same = true;
            string detail;

            for (size_t i = 0; i < got[0].size() && same; i++)
                if (memcmp(&got[1][i], &got[0][i], sizeof(float)) != 0)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(got[1][i]) + " against " + num(got[0][i]);
                }

            okOrFail(same, "delay::chorus: `mix = 0' is the dry signal, to "
                           "the bit", detail);
        }
    }

    /* And a still tap at full mix is the line and nothing else. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ch", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath,
                    chorusGraph((float)f0, 0, 0, delay, 1, 1), watch, 256,
                    20000, got, why))
            fail("delay::chorus renders", why);
        else
        {
            bool same = true;
            string detail;

            for (size_t i = (size_t)delay; i < got[0].size() && same; i++)
                if (fabs(got[1][i] - got[0][i - (size_t)delay]) >
                    TH_MAX * 1e-6)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(got[1][i]) + " against " +
                             num(got[0][i - (size_t)delay]);
                }

            okOrFail(same, "delay::chorus: a tap that does not move is a "
                           "delay of `delay' samples", detail);
        }
    }

    /* ---- and `feedback' makes it a flanger ---- */

    /* The tap's output back into the line's write, which turns one
     * reflection into a resonance. With the LFO stopped it is a plain comb
     * and the claim can be read straight off the samples: a click comes back
     * every `delay' samples for as long as the loop holds it, each repeat
     * `feedback' times the one before.
     *
     * Peaks per block rather than single samples because env::ad's burst is
     * thirty-two samples wide; the block is the delay, so a block holds one
     * repeat.
     */
    {
        const float tap = 200, fb = 0.9f;
        vector<float> got;
        string why;

        if (!render1(pluginPath, chorusClickGraph(tap, fb), "ch", "out", 256,
                     (unsigned)(tap * 9), got, why))
            fail("delay::chorus renders", why);
        else
        {
            vector<double> repeat;

            /* From `tap', which is where the first one lands: the output is
               the tap alone at `mix = 1', so nothing is heard before it. */
            for (size_t b = 1; (b + 1) * (size_t)tap <= got.size(); b++)
                repeat.push_back(peak(vector<float>(
                    got.begin() + b * (size_t)tap,
                    got.begin() + (b + 1) * (size_t)tap), 0));

            bool rings = repeat.size() > 6 && repeat[0] > 0;
            string detail = "saw " + num((double)repeat.size()) + " repeats";

            for (size_t b = 1; b < repeat.size() && rings; b++)
                if (fabs(repeat[b] - repeat[b - 1] * fb) > repeat[0] * 0.01)
                {
                    rings = false;
                    detail = "repeat " + num((double)b) + " was " +
                             num(repeat[b]) + ", wanted " +
                             num(repeat[b - 1] * fb);
                }

            okOrFail(rings, "delay::chorus: `feedback' rings a click every "
                            "`delay' samples, each repeat `feedback' of the "
                            "last", detail);
        }
    }

    /* And the sign of it, which is the difference between the two flangers.
     * Negative feedback writes the inverse back, so every other repeat comes
     * out upside down -- the comb's peaks move to where its notches were,
     * which is the hollow one.
     */
    {
        const float tap = 200;
        vector<float> got;
        string why;

        if (!render1(pluginPath, chorusClickGraph(tap, -0.9f), "ch", "out",
                     256, (unsigned)(tap * 5), got, why))
            fail("delay::chorus renders", why);
        else
        {
            bool alternates = true;
            string detail;

            for (size_t b = 1; b < 4 && alternates; b++)
            {
                /* The extreme of the block, sign and all: the repeats are
                   the same shape and only their polarity is in question. */
                double top = 0;

                for (size_t i = b * (size_t)tap;
                     i < (b + 1) * (size_t)tap && i < got.size(); i++)
                    if (fabs(got[i]) > fabs(top))
                        top = got[i];

                /* The first repeat is the input read back once and has not
                   been through the feedback at all, so it is upright; each
                   trip after it carries the sign again. */
                if ((b % 2 == 1) ? !(top > 0) : !(top < 0))
                {
                    alternates = false;
                    detail = "repeat " + num((double)b) + " peaked at " +
                             num(top);
                }
            }

            okOrFail(alternates, "delay::chorus: negative `feedback' flips "
                                 "every other repeat", detail);
        }
    }

    /* ---- and a `feedback' that is not a number is no feedback ---- */

    /* The inert end of a signed gain is the middle. thClampArg answers a
     * non-finite with `lo', which for this arg is -0.95 -- so the reading
     * that let a NaN through the door would turn the loudest inverted comb
     * the node has on, and the line being its own input would keep it on.
     * Against `feedback = 0' sample for sample, because "inert" is not a
     * level here, it is the other render exactly.
     */
    {
        const float tap = 200;
        vector<float> off, nan;
        string why;

        if (!render1(pluginPath, chorusClickGraph(tap, 0), "ch", "out", 256,
                     (unsigned)(tap * 5), off, why) ||
            !render1(pluginPath,
                     chorusClickGraph(tap,
                                      std::numeric_limits<float>::quiet_NaN()),
                     "ch", "out", 256, (unsigned)(tap * 5), nan, why))
            fail("delay::chorus renders", why);
        else
        {
            double worst = 0;

            for (size_t i = 0; i < off.size() && i < nan.size(); i++)
                if (fabs(off[i] - nan[i]) > worst)
                    worst = fabs(off[i] - nan[i]);

            okOrFail(off.size() == nan.size() && peak(off, 0) > 0 &&
                     worst == 0,
                     "delay::chorus: a `feedback' that is not a number is "
                     "no feedback, and not the bottom of its range",
                     "off by " + num(worst) + " at worst");
        }
    }

    /* ---- the tap is read between samples ---- */

    /* Which is what fx/flanger.dsp's through-zero copy rests on: its dry
     * copy is a second delay::chorus held still at `Delay', and it is the
     * same node as the sweeping one precisely so that the two agree on
     * where `Delay' is. `Delay' in milliseconds is a whole number of
     * samples only by accident -- 1.5 ms is 66.15 of them at 44100 -- so a
     * node that truncated would center the sweep somewhere the tap does
     * not turn around.
     *
     * Half a sample back is exactly half of each neighbour, because the
     * read is linear between the two. Exactly: the same two samples are
     * being averaged, so there is no rounding to allow for.
     */
    {
        const float tap = 200;
        vector<float> lo, mid, hi;
        string why;

        if (!render1(pluginPath, chorusClickGraph(tap, 0), "ch", "out", 256,
                     (unsigned)(tap * 3), lo, why) ||
            !render1(pluginPath, chorusClickGraph(tap + 0.5f, 0), "ch", "out",
                     256, (unsigned)(tap * 3), mid, why) ||
            !render1(pluginPath, chorusClickGraph(tap + 1, 0), "ch", "out",
                     256, (unsigned)(tap * 3), hi, why))
            fail("delay::chorus renders", why);
        else
        {
            double worst = 0;

            for (size_t i = 0; i < mid.size() && i < lo.size() &&
                               i < hi.size(); i++)
                if (fabs((double)mid[i] - ((double)lo[i] + hi[i]) / 2) > worst)
                    worst = fabs((double)mid[i] - ((double)lo[i] + hi[i]) / 2);

            okOrFail(peak(mid, 0) > 0 && worst < 1e-3,
                     "delay::chorus: a tap half a sample back is half of "
                     "each neighbour, so a fractional `delay' is one",
                     "off by " + num(worst) + " at worst");
        }
    }

    windowsAgree(pluginPath,
                 chorusGraph((float)f0, (float)rate, depth, delay, 0.5f, 3),
                 "ch", "out",
                 "delay::chorus: the same taps at one sample a window and "
                 "at five hundred");

    /* The line is its own input with feedback on it, so a window boundary
       has one more thing to get wrong. */
    windowsAgree(pluginPath, chorusClickGraph(delay, 0.8f), "ch", "out",
                 "delay::chorus: the same comb, fed back, at one sample a "
                 "window and at five hundred");
}

/* ---- filt::comb --------------------------------------------------------- */

/* What the node claims, and what each check is for:
 *
 *   - `damp = 0' is the node as it was: one sample in comes back out
 *     every `rate/freq' samples, `feedback' quieter each turn and
 *     nothing at all in between, which is the closed form a bare delay
 *     line with a gain in it has;
 *
 *   - `damp' up, the harmonics above the fundamental decay faster than
 *     it does -- the whole of the difference between a pipe and a
 *     string, measured as two band-pass sums at two times;
 *
 *   - the ring is at `freq' to within a cent from 40 Hz to 2 kHz,
 *     damped or not, which needs the read between samples and needs the
 *     damper's own delay taken off the line's;
 *
 *   - and a window boundary is not an event.
 */

/* env::ad with no attack and a decay of one sample is exactly one sample
   at full scale, which is the input whose output can be named. */
static vector<NodeSpec> combImpulseGraph (float freq, float feedback,
                                          float damp)
{
    vector<NodeSpec> spec;
    NodeSpec src, comb;

    src.name = "src";
    src.spelling = "env/ad";

    Value a = { "a", 0 };
    Value d = { "d", 1 };
    Value p = { "p", TH_MAX };

    src.values.push_back(a);
    src.values.push_back(d);
    src.values.push_back(p);

    comb.name = "comb";
    comb.spelling = "filt/comb";

    Value f = { "freq", freq };
    Value fb = { "feedback", feedback };
    Value dp = { "damp", damp };
    Value sz = { "size", (float)TH_DEFAULT_SAMPLES };
    Wire  in = { "in", "src", "out" };

    comb.values.push_back(f);
    comb.values.push_back(fb);
    comb.values.push_back(dp);
    comb.values.push_back(sz);
    comb.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(comb);

    return spec;
}

/* A few cycles of a sine at the comb's own frequency and then silence.
   The loop is linear, so a burst at the fundamental leaves the
   fundamental ringing and next to nothing on the harmonics above it: the
   tail is a decaying sine whose zero crossings are the loop's period and
   not some sum of partials. */
static vector<NodeSpec> combSineGraph (float freq, float feedback, float damp,
                                       float cycles)
{
    vector<NodeSpec> spec = combImpulseGraph(freq, feedback, damp);
    NodeSpec osc, gate;

    /* Half the burst is the attack and half is the decay, so the window is
       a bell and not a pair of steps: a burst switched on and off puts a
       click's worth of energy into every other mode of the loop, and a
       mode that decays at its own rate under the one being measured is a
       waveform whose shape keeps changing. */
    spec[0].name = "burst";
    spec[0].values[0].value = cycles * TH_DEFAULT_SAMPLES / (2 * freq);
    spec[0].values[1].value = cycles * TH_DEFAULT_SAMPLES / (2 * freq);

    osc.name = "osc";
    osc.spelling = "osc/simple";

    Value f = { "freq", freq };
    Value w = { "waveform", 0 };            /* sine */
    Value a = { "amp", TH_MAX };

    osc.values.push_back(f);
    osc.values.push_back(w);
    osc.values.push_back(a);

    /* A math::mul and not the oscillator's own `amp': an `amp' of zero is
       the one the callback reads as full scale, so an envelope on it is a
       burst that never ends. */
    gate.name = "src";
    gate.spelling = "math/mul";

    Wire g0 = { "in0", "osc", "out" };
    Wire g1 = { "in1", "burst", "out" };

    gate.wires.push_back(g0);
    gate.wires.push_back(g1);

    spec.push_back(osc);
    spec.push_back(gate);

    return spec;
}

/* And a noise burst, which puts energy on every partial the loop has, with
   a band-pass on the fundamental and another on the octave above it to
   watch them go. `res' high enough that neither band hears the other:
   at a Q of 25 an octave away is 30 dB down. */
static vector<NodeSpec> combNoiseGraph (float freq, float feedback, float damp)
{
    vector<NodeSpec> spec = combImpulseGraph(freq, feedback, damp);
    NodeSpec src, gate, band1, band2;

    spec[0].name = "burst";
    spec[0].values[1].value = 64;

    src.name = "noise";
    src.spelling = "osc/noise";

    Value color = { "color", 0 };           /* white */
    Value amp = { "amp", TH_MAX };

    src.values.push_back(color);
    src.values.push_back(amp);

    /* Gated the same way, and for the same reason. */
    gate.name = "src";
    gate.spelling = "math/mul";

    Wire g0 = { "in0", "noise", "out" };
    Wire g1 = { "in1", "burst", "out" };

    gate.wires.push_back(g0);
    gate.wires.push_back(g1);

    band1.name = "band1";
    band1.spelling = "filt/svf";

    Value c1 = { "cutoff", freq };
    Value r1 = { "res", 0.98f };
    Wire  i1 = { "in", "comb", "out" };

    band1.values.push_back(c1);
    band1.values.push_back(r1);
    band1.wires.push_back(i1);

    band2 = band1;
    band2.name = "band2";
    band2.values[0].value = 2 * freq;

    spec.push_back(src);
    spec.push_back(gate);
    spec.push_back(band1);
    spec.push_back(band2);

    return spec;
}

/* Every upward zero crossing after `from', at sub-sample resolution: where
   the line between the two samples either side of it cuts zero. The
   envelope does not move them -- a decaying sine crosses zero exactly
   where the sine does -- so the spacing is the period however far the
   ring has fallen. */
static vector<double> upCrossings (const vector<float> &v, size_t from,
                                   size_t to)
{
    vector<double> at;

    for (size_t i = from + 1; i < to && i < v.size(); i++)
        if (v[i - 1] <= 0 && v[i] > 0)
            at.push_back((double)(i - 1) +
                         (double)-v[i - 1] / ((double)v[i] - v[i - 1]));

    return at;
}

/* The signal with its own offset taken off it.
 *
 * The loop rings at every multiple of `freq' and at nothing at all as well:
 * DC goes round it with no damping whatever `damp' says, so under a decaying
 * tone there is an offset decaying more slowly, and an offset walks a zero
 * crossing. The mean of one period of a periodic signal is exactly its
 * offset, so a running mean one period long is what to take off -- and it is
 * symmetric about the sample it is taken from, so unlike a filter it moves
 * nothing in time and has no transient of its own to wait out.
 */
static vector<float> deOffset (const vector<float> &v, double period)
{
    const size_t len = (size_t)(period + 0.5);
    vector<double> sum(v.size() + 1, 0.0);
    vector<float> out(v.size(), 0.0f);

    for (size_t i = 0; i < v.size(); i++)
        sum[i + 1] = sum[i] + v[i];

    for (size_t i = len; i + len < v.size(); i++)
        out[i] = (float)(v[i] - (sum[i + len / 2 + 1] - sum[i - len / 2]) /
                                (len / 2 * 2 + 1));

    return out;
}

/* The pitch the loop settled at, in hertz, or 0 if the tail was not a tone
   with `want' cycles in it. */
static double ringPitch (const vector<float> &v, size_t from, size_t to,
                         unsigned want)
{
    const vector<double> at = upCrossings(v, from, to);

    if (at.size() < want)
        return 0;

    return (double)TH_DEFAULT_SAMPLES * (at.size() - 1) /
           (at[at.size() - 1] - at[0]);
}

static double cents (double got, double want)
{
    return 1200 * log(got / want) / log(2.0);
}

static void checkComb (const string &pluginPath)
{
    const double rate = TH_DEFAULT_SAMPLES;

    /* ---- `damp = 0' is a delay line with a gain in it ---- */

    /* A hundred samples and a half, so that the read lands on a sample and
       the answer is exact rather than nearly: the impulse comes back at
       every multiple of the period at `feedback' to the power of the turn,
       and every other sample is zero. Bit for bit, which is what makes this
       a check on the node an existing graph still has rather than on a
       tolerance. */
    {
        const unsigned period = 100;
        const float feedback = 0.5f;
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     combImpulseGraph((float)(rate / period), feedback, 0),
                     "comb", "out", 256, 2000, out, why))
            fail("filt::comb renders", why);
        else
        {
            bool exact = true;
            string detail;
            float want = 1;

            for (size_t i = 0; i < out.size() && exact; i++)
            {
                float expect;

                if (i % period == 0 && i)
                    want *= feedback;

                expect = (i % period) ? 0 : want;

                if (memcmp(&out[i], &expect, sizeof(float)) != 0)
                {
                    exact = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(out[i]) + " for " + num(expect);
                }
            }

            okOrFail(exact, "filt::comb: at `damp = 0' an impulse comes back "
                            "every period, `feedback' quieter, and nothing "
                            "comes back in between", detail);
        }
    }

    /* ---- `damp' takes the harmonics first ---- */

    /* A noise burst puts the same energy on the fundamental and on the
       octave above it; what the two bands measure is which one is left
       later. With no damping the loop is a gain and they go together, and
       that is the control: the same graph, the same noise, one knob. */
    {
        static const float damps[] = { 0, 0.5f };

        const float freq = (float)(rate / 100);
        double fell[2] = { 0, 0 };
        bool bad = false;

        for (size_t c = 0; c < 2 && !bad; c++)
        {
            vector<Watch> watch;
            vector< vector<float> > got;
            string why;

            Watch w0 = { "band1", "out_band" };
            Watch w1 = { "band2", "out_band" };

            watch.push_back(w0);
            watch.push_back(w1);

            if (!render(pluginPath, combNoiseGraph(freq, 0.995f, damps[c]),
                        watch, 256, (unsigned)(rate / 2), got, why))
            {
                fail("filt::comb renders", why);
                bad = true;
                break;
            }

            /* Two fiftieths of a second each, an eighth of a second apart:
               eighty-eight turns of the loop between them. */
            for (size_t b = 0; b < 2; b++)
            {
                const vector<float> &v = got[b];
                vector<float> early(v.begin() + (size_t)(rate / 10),
                                    v.begin() + (size_t)(rate / 10 +
                                                         rate / 50));
                vector<float> late(v.begin() + (size_t)(rate / 4),
                                   v.begin() + (size_t)(rate / 4 + rate / 50));
                const double was = rms(early, 0);

                fell[b] = was > 0 ? rms(late, 0) / was : 0;
            }

            if (damps[c] == 0)
                okOrFail(fell[0] > 0 && fabs(fell[1] / fell[0] - 1) < 0.15,
                         "filt::comb: with no damping the octave decays with "
                         "the fundamental",
                         "the fundamental kept " + num(fell[0]) +
                         " of itself and the octave " + num(fell[1]));
            else
                okOrFail(fell[0] > 0 && fell[1] < fell[0] * 0.7,
                         "filt::comb: `damp' takes the octave faster than "
                         "the fundamental, which is what a string does",
                         "the fundamental kept " + num(fell[0]) +
                         " of itself and the octave " + num(fell[1]));
        }
    }

    /* ---- and it rings at `freq' ---- */

    /* Within a cent, across five and a half octaves, damped and not. A
       whole-sample read is a period rounded to a sample: at 2 kHz that is
       78 cents, and it is why a comb was a reverb and not an instrument.
       The damped pass is the second half of the same claim -- a filter in
       a loop is a delay too, and a string that goes flat as it is damped
       is a string that cannot play a scale. */
    {
        static const float hz[] = { 40, 110, 441, 1000, 2000 };
        static const float damps[] = { 0, 0.5f };

        for (size_t d = 0; d < 2; d++)
        {
            bool tuned = true;
            string detail;

            for (size_t c = 0; c < sizeof(hz) / sizeof(hz[0]) && tuned; c++)
            {
                const double period = rate / hz[c];
                /* The burst, and two more periods for the loop to settle
                   into its own tail, then forty turns of it. */
                const size_t settle = (size_t)(12 * period);
                const size_t last = settle + (size_t)(40 * period);
                vector<float> out;
                string why;
                double pitch;

                if (!render1(pluginPath,
                             combSineGraph(hz[c], 0.995f, damps[d], 10),
                             "comb", "out", 256, (unsigned)(last + period),
                             out, why))
                {
                    fail("filt::comb renders", why);
                    return;
                }

                pitch = ringPitch(deOffset(out, period), settle, last, 30);

                if (!(pitch > 0) || fabs(cents(pitch, hz[c])) > 1)
                {
                    tuned = false;
                    detail = num(hz[c]) + " Hz rang at " + num(pitch) +
                             " Hz, " + num(cents(pitch, hz[c])) + " cents "
                             "out";
                }
            }

            okOrFail(tuned, damps[d] == 0
                            ? "filt::comb: the ring is `freq' to within a "
                              "cent from 40 Hz to 2 kHz"
                            : "filt::comb: and it is still `freq' with the "
                              "damper in the loop",
                     detail);
        }
    }

    /* The line, the write head and the damper's own memory all cross a
       window boundary, and the line is its own input: three things to get
       wrong and one measurement that sees all of them. */
    windowsAgree(pluginPath, combNoiseGraph((float)(rate / 100), 0.995f, 0.5f),
                 "comb", "out",
                 "filt::comb: the same ring at one sample a window and at "
                 "five hundred");

    /* Changing size reallocates the delay line. Its one-pole memory must be
       cleared with it, or silence on the new line starts with an old tail. */
    {
        thSynth synth(pluginPath, 1, TH_DEFAULT_SAMPLES);
        thSynthTree tree("comb-resize", &synth);
        vector<NodeSpec> spec = combImpulseGraph((float)(rate / 4), 0.9f,
                                                  0.5f);
        string why;

        spec[1].values[3].value = 8;

        if (!buildGraph(synth, tree, spec, why))
            fail("filt::comb resize graph loads", why);
        else
        {
            thNode *comb = tree.findNode("comb");
            float before = 0;

            for (int i = 0; i < 13; i++)
            {
                tree.setActiveNodes();
                tree.process(1);
                before = (*comb->getArg("out"))[0];
            }

            comb->setArg("size", 16.0f);
            tree.setActiveNodes();
            tree.process(1);

            const float after = (*comb->getArg("out"))[0];

            okOrFail(before > 0 && after == 0,
                     "filt::comb: resizing clears the whole feedback state",
                     "before " + num(before) + ", after " + num(after));
        }
    }
}

/* ---- delay::pitchshift -------------------------------------------------- */

/* A power spectrum of `n' samples from `from', `n' a power of two, under
 * a Hann window: a radix-2 transform, since what is wanted here is every
 * bin and not the one or two that bin() is for. */
static vector<double> powerSpectrum (const vector<float> &v, size_t from,
                                     size_t n)
{
    vector<double> re(n), im(n, 0);

    for (size_t i = 0; i < n; i++)
        re[i] = (from + i < v.size() ? v[from + i] : 0) *
                (0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n));

    for (size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double step = -2.0 * M_PI / (double)len;

        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++)
            {
                const double wr = cos(step * k), wi = sin(step * k);
                const size_t a = i + k, b = i + k + len / 2;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }

    vector<double> power(n / 2);

    for (size_t k = 0; k < n / 2; k++)
        power[k] = re[k] * re[k] + im[k] * im[k];

    return power;
}

static vector<NodeSpec> shiftGraph (float hz, float ratio, float window,
                                    float mix)
{
    vector<NodeSpec> spec;
    NodeSpec src, ps;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 0 };            /* sine */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    ps.name = "ps";
    ps.spelling = "delay/pitchshift";

    Value r = { "ratio", ratio };
    Value wn = { "window", window };
    Value mx = { "mix", mix };
    Wire  in = { "in", "src", "out" };

    ps.values.push_back(r);
    ps.values.push_back(wn);
    ps.values.push_back(mx);
    ps.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(ps);

    return spec;
}

/* The share of a spectrum's power within a semitone of `hz'. */
static double shareNear (const vector<double> &spectrum, size_t n, double hz)
{
    const double hzPerBin = (double)TH_DEFAULT_SAMPLES / (double)n;
    const double lo = hz * pow(2.0, -1.0 / 12), hi = hz * pow(2.0, 1.0 / 12);
    double near = 0, all = 0;

    for (size_t k = 1; k < spectrum.size(); k++)
    {
        all += spectrum[k];

        if (k * hzPerBin >= lo && k * hzPerBin <= hi)
            near += spectrum[k];
    }

    return all > 0 ? near / all : 0;
}

static void checkPitchshift (const string &pluginPath)
{
    /* Fifty milliseconds less a sample, which makes it even: half of it
       is then a whole number of samples, and `ratio = 1' can be checked
       to the bit. */
    const float window = 2204;

    /* ---- an octave up is an octave up ---- */

    /* A 440 Hz sine at `ratio = 2', wet only, over a settled second and a
       half: most of what comes out has to be within a semitone of 880 Hz,
       and what is left at 440 has to be under a tenth of it. The heads'
       crossfade puts sidebands twenty hertz either side of 880, which
       are inside the semitone and are the warble the effect is known for. */
    {
        static const struct { float ratio; double want; } cases[] = {
            { 2, 880 }, { 0.5f, 220 }, { 1.5f, 660 }
        };
        bool good = true;
        string detail;

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            vector<float> got;
            string why;

            if (!render1(pluginPath, shiftGraph(440, cases[c].ratio, window,
                                                1),
                         "ps", "out", 256, 2 * TH_DEFAULT_SAMPLES, got, why))
            {
                fail("delay::pitchshift renders", why);
                return;
            }

            const vector<double> spectrum =
                powerSpectrum(got, TH_DEFAULT_SAMPLES / 4, 65536);
            const double there = shareNear(spectrum, 65536, cases[c].want);
            const double left = shareNear(spectrum, 65536, 440);

            if (!(there > 0.8 && left < 0.1 * there))
            {
                good = false;
                detail = "at a ratio of " + num(cases[c].ratio) + ", " +
                         num(there) + " of the power was near " +
                         num(cases[c].want) + " Hz and " + num(left) +
                         " was left at 440";
            }
        }

        okOrFail(good, "delay::pitchshift: a sine comes out at `ratio' times "
                       "its frequency, with next to nothing left where it "
                       "was", detail);
    }

    /* ---- and `ratio = 1' is a delay of half the window ---- */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ps", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, shiftGraph(440, 1, window, 1), watch, 256,
                    20000, got, why))
            fail("delay::pitchshift renders", why);
        else
        {
            const size_t late = (size_t)window / 2;
            bool same = true;
            string detail;

            for (size_t i = late; i < got[0].size() && same; i++)
                if (memcmp(&got[1][i], &got[0][i - late],
                           sizeof(float)) != 0)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(got[1][i]) + " against " +
                             num(got[0][i - late]);
                }

            okOrFail(same, "delay::pitchshift: `ratio = 1' is the input "
                           "half a window late, to the bit", detail);
        }
    }

    /* ---- `mix = 0' is the dry signal ---- */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "src", "out" };
        Watch w1 = { "ps", "out" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, shiftGraph(440, 2, window, 0), watch, 256,
                    20000, got, why))
            fail("delay::pitchshift renders", why);
        else
            okOrFail(memcmp(&got[0][0], &got[1][0],
                            got[0].size() * sizeof(float)) == 0,
                     "delay::pitchshift: `mix = 0' is the dry signal, to the "
                     "bit", "");
    }

    /* ---- finite at every corner ---- */
    {
        static const float ratios[] = { -1, 0, 0.25f, 4, 100 };
        static const float windows[] = { -5, 0, 1, 882, 8820, 1e9f };
        bool finite = true;
        string detail;

        for (float r : ratios)
            for (float w : windows)
            {
                vector<float> got;
                string why;

                if (!render1(pluginPath, shiftGraph(440, r, w, 1), "ps",
                             "out", 256, TH_DEFAULT_SAMPLES / 2, got, why))
                {
                    fail("delay::pitchshift renders", why);
                    return;
                }

                if (finite && !(allFinite(got) && peak(got, 0) < 1.5))
                {
                    finite = false;
                    detail = "ratio " + num(r) + ", window " + num(w) +
                             ": peak " + num(peak(got, 0));
                }
            }

        okOrFail(finite, "delay::pitchshift: finite and in range at every "
                         "corner", detail);
    }

    windowsAgree(pluginPath, shiftGraph(440, 2, window, 0.5f), "ps", "out",
                 "delay::pitchshift: the same shift at one sample a window "
                 "and at five hundred");
}

/* ---- delay::fdn --------------------------------------------------------- */

/* One sample at full scale into the network, as the comb's impulse is:
   what comes out is the network's own answer and nothing else. */
static vector<NodeSpec> fdnGraph (float size, float decay, float damping,
                                  float mod, float rate, float diffuse,
                                  float shimmer = 0, float interval = 0)
{
    vector<NodeSpec> spec;
    NodeSpec src, fdn;

    src.name = "src";
    src.spelling = "env/ad";

    Value a = { "a", 0 };
    Value d = { "d", 1 };
    Value p = { "p", TH_MAX };

    src.values.push_back(a);
    src.values.push_back(d);
    src.values.push_back(p);

    fdn.name = "fdn";
    fdn.spelling = "delay/fdn";

    Value sz = { "size", size };
    Value dc = { "decay", decay };
    Value dm = { "damping", damping };
    Value md = { "mod", mod };
    Value rt = { "rate", rate };
    Value df = { "diffuse", diffuse };
    Value sh = { "shimmer", shimmer };
    Value iv = { "interval", interval };
    Wire  in = { "in", "src", "out" };

    fdn.values.push_back(sz);
    fdn.values.push_back(dc);
    fdn.values.push_back(dm);
    fdn.values.push_back(md);
    fdn.values.push_back(rt);
    fdn.values.push_back(df);
    fdn.values.push_back(sh);
    fdn.values.push_back(iv);
    fdn.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(fdn);

    return spec;
}

/* How long a response takes to fall sixty decibels, read off its slope.
 *
 * The energy in blocks of fifty milliseconds, in decibels, fitted with a
 * straight line from `from' to `to' seconds: one realization of a tail is
 * noise, and a block's energy wanders a decibel or two either side of the
 * curve, but a least-squares line through hundreds of them does not. */
static double decayTime (const vector<float> &v, double from, double to)
{
    const size_t block = TH_DEFAULT_SAMPLES / 20;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;

    for (size_t at = (size_t)(from * TH_DEFAULT_SAMPLES);
         at + block <= v.size() && at < (size_t)(to * TH_DEFAULT_SAMPLES);
         at += block)
    {
        double e = 0;

        for (size_t i = at; i < at + block; i++)
            e += (double)v[i] * v[i];

        const double x = (double)at / TH_DEFAULT_SAMPLES;
        const double y = 10.0 * log10(e + 1e-300);

        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        n++;
    }

    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);

    return -60.0 / slope;
}

/* Sixteen bands a third of an octave wide, from 250 Hz to 10 kHz, each
 * the sum of every bin of the spectrum across it. What is returned is the
 * worst band's excess, in decibels, over the mean of its two neighbors.
 *
 * Sums and not bins, because the tail of any reverb, good or bad, is
 * noise, and the power in one bin of noise is exponentially distributed:
 * a bin six decibels over its neighbors is a one-in-fifty event in a
 * perfectly smooth tail. Summed over the hundred-odd bins of the
 * narrowest band, chance moves a band by half a decibel, so a band six
 * over its neighbors is a resonance. */
static double bandExcess (const vector<float> &v, size_t from, size_t n)
{
    const int bands = 16;
    const vector<double> spectrum = powerSpectrum(v, from, n);
    const double hzPerBin = (double)TH_DEFAULT_SAMPLES / (double)n;
    vector<double> power(bands, 0);

    for (int b = 0; b < bands; b++)
    {
        const double lo = 250.0 * pow(2.0, b / 3.0);
        const double hi = lo * pow(2.0, 1.0 / 3.0);

        for (size_t k = (size_t)ceil(lo / hzPerBin);
             k < spectrum.size() && k * hzPerBin < hi; k++)
            power[b] += spectrum[k];
    }

    double worst = -1e9;

    for (int b = 1; b + 1 < bands; b++)
    {
        const double around = (power[b - 1] + power[b + 1]) / 2;
        const double excess = 10.0 * log10(power[b] / around);

        if (excess > worst)
            worst = excess;
    }

    return worst;
}

/* How many samples stand clear of `floorAt'. */
static double echoes (const vector<float> &v, double floorAt)
{
    double n = 0;

    for (size_t i = 0; i < v.size(); i++)
        if (fabs(v[i]) > floorAt)
            n++;

    return n;
}

/* The 8 kHz third-octave band against the 250 Hz one, in decibels, over a
   second and a half from a second in: how dark the tail has gone. */
static double tilt3 (const vector<float> &v)
{
    const vector<double> spectrum =
        powerSpectrum(v, TH_DEFAULT_SAMPLES, 65536);
    const double hzPerBin = (double)TH_DEFAULT_SAMPLES / 65536;
    double lo = 0, hi = 0;

    for (size_t k = 0; k < spectrum.size(); k++)
    {
        const double hz = k * hzPerBin;

        if (hz >= 250 && hz < 250 * pow(2.0, 1.0 / 3.0))
            lo += spectrum[k];

        if (hz >= 8000 && hz < 8000 * pow(2.0, 1.0 / 3.0))
            hi += spectrum[k];
    }

    /* Per hertz, since the upper band is thirty-two times as wide. */
    return 10 * log10((hi / 32) / lo);
}

static void checkFdn (const string &pluginPath)
{
    /* ---- the tail falls sixty decibels in `decay' seconds ---- */

    /* With the damping off, so that every frequency is asked to fall at
       the rate the gains set and none is falling faster; the slope is
       read from a fifth of a second in, after the attack has built, to
       where it is thirty-six decibels down. */
    {
        static const float want[] = { 1, 4 };
        bool good = true;
        string detail;

        for (size_t c = 0; c < sizeof(want) / sizeof(want[0]); c++)
        {
            vector<float> got;
            string why;

            if (!render1(pluginPath, fdnGraph(1, want[c], 0, 0, 0, 0.7f),
                         "fdn", "out", 256,
                         (unsigned)(want[c] * TH_DEFAULT_SAMPLES), got, why))
            {
                fail("delay::fdn renders", why);
                return;
            }

            const double t = decayTime(got, 0.2, 0.2 + want[c] * 0.6);

            if (!(fabs(t / want[c] - 1) < 0.1))
            {
                good = false;
                detail = "a decay of " + num(want[c]) + " s fell sixty "
                         "decibels in " + num(t) + " s";
            }
        }

        okOrFail(good, "delay::fdn: the tail falls sixty decibels in "
                       "`decay' seconds", detail);
    }
    /* ---- and a minute is a minute ---- */

    /* An ambient tail of sixty seconds, which a comb bank cannot reach
       without ringing, and modulated, since that is how one would be run.
       Twenty seconds of it is twenty decibels, which is enough slope to
       read. */
    {
        vector<float> got;
        string why;

        if (!render1(pluginPath, fdnGraph(1, 60, 0, 12, 0.5f, 0.7f), "fdn",
                     "out", 256, 20 * TH_DEFAULT_SAMPLES, got, why))
            fail("delay::fdn renders", why);
        else
        {
            const double t = decayTime(got, 0.2, 20);

            okOrFail(allFinite(got) && fabs(t / 60 - 1) < 0.1,
                     "delay::fdn: a decay of sixty seconds is sixty seconds, "
                     "modulated",
                     "fell sixty decibels in " + num(t) + " s");
        }
    }

    /* ---- the first hundred milliseconds are dense ---- */

    /* An echo is a sample well clear of the floor, here a thousandth of
       the response's peak. Eight lines alone would put eight of them in
       the first pass; the diffusers have to turn each into a burst, so
       that most of the first tenth of a second is already sounding. The
       comparison with fx/hall.dsp is fxcheck's, which loads it. */
    {
        vector<float> smeared, bare;
        string why;

        if (!render1(pluginPath, fdnGraph(1, 2, 0, 0, 0, 0.7f), "fdn", "out",
                     256, TH_DEFAULT_SAMPLES / 10, smeared, why) ||
            !render1(pluginPath, fdnGraph(1, 2, 0, 0, 0, 0), "fdn", "out",
                     256, TH_DEFAULT_SAMPLES / 10, bare, why))
            fail("delay::fdn renders", why);
        else
        {
            const double a = echoes(smeared, peak(smeared, 0) / 1000);
            const double b = echoes(bare, peak(bare, 0) / 1000);

            okOrFail(a > 10 * b,
                     "delay::fdn: the diffusers put ten times the echoes "
                     "into the first hundred milliseconds",
                     num(a) + " echoes with them, " + num(b) + " without");
        }
    }

    /* ---- the tail has no resonance in it ---- */

    /* bandExcess over a second and a half of a thirty-second tail, taken
       from a second in so that it is the tail and not the attack. The
       control is a comb at 400 Hz, which is a reverb made of one line:
       its harmonics are wider apart than the low bands, so some bands
       hold a peak and some hold none, and it comes out tens of decibels
       over. */
    {
        vector<float> still, moving, comb;
        string why;

        if (!render1(pluginPath, fdnGraph(1, 30, 0, 0, 0, 0.7f), "fdn",
                     "out", 256, 3 * TH_DEFAULT_SAMPLES, still, why) ||
            !render1(pluginPath, fdnGraph(1, 30, 0, 12, 0.5f, 0.7f), "fdn",
                     "out", 256, 3 * TH_DEFAULT_SAMPLES, moving, why) ||
            !render1(pluginPath, combImpulseGraph(400, 0.999f, 0), "comb",
                     "out", 256, 3 * TH_DEFAULT_SAMPLES, comb, why))
            fail("delay::fdn renders", why);
        else
        {
            const double s0 = bandExcess(still, TH_DEFAULT_SAMPLES, 65536);
            const double s1 = bandExcess(moving, TH_DEFAULT_SAMPLES, 65536);
            const double c = bandExcess(comb, TH_DEFAULT_SAMPLES, 65536);

            okOrFail(c > 6, "delay::fdn: the resonance check sees a comb's",
                     "a 400 Hz comb's worst band was only " + num(c) +
                     " dB over its neighbors");
            okOrFail(s0 < 6 && s1 < 6,
                     "delay::fdn: no band of a thirty-second tail is six "
                     "decibels over its neighbors",
                     num(s0) + " dB still, " + num(s1) + " dB modulated");
        }

        /* And the modulation does not darken it: the top band against the
           bottom, moving against still. This is what linear interpolation
           in the loop failed, by thirty decibels. */
        if (!still.empty() && !moving.empty())
        {
            const double t0 = tilt3(still), t1 = tilt3(moving);

            okOrFail(fabs(t1 - t0) < 3,
                     "delay::fdn: modulation leaves the top of the tail "
                     "where it was",
                     "8 kHz against 250 Hz was " + num(t0) + " dB still and " +
                     num(t1) + " dB modulated");
        }
    }

    /* ---- damping takes the top faster ---- */
    {
        vector<float> open, damped;
        string why;

        if (!render1(pluginPath, fdnGraph(1, 4, 0, 0, 0, 0.7f), "fdn",
                     "out", 256, 3 * TH_DEFAULT_SAMPLES, open, why) ||
            !render1(pluginPath, fdnGraph(1, 4, 3000, 0, 0, 0.7f), "fdn",
                     "out", 256, 3 * TH_DEFAULT_SAMPLES, damped, why))
            fail("delay::fdn renders", why);
        else
        {
            const double t0 = tilt3(open), t1 = tilt3(damped);

            okOrFail(t1 < t0 - 20,
                     "delay::fdn: `damping' takes the top of the tail "
                     "faster than the bottom",
                     "8 kHz against 250 Hz was " + num(t0) + " dB open and " +
                     num(t1) + " dB damped");
        }
    }

    /* ---- the two sides are the same tail, uncorrelated ---- */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "fdn", "out" };
        Watch w1 = { "fdn", "out2" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, fdnGraph(1, 4, 6000, 12, 0.5f, 0.7f), watch,
                    256, 2 * TH_DEFAULT_SAMPLES, got, why))
            fail("delay::fdn renders", why);
        else
        {
            double ll = 0, rr = 0, lr = 0;

            for (size_t i = TH_DEFAULT_SAMPLES / 5; i < got[0].size(); i++)
            {
                ll += (double)got[0][i] * got[0][i];
                rr += (double)got[1][i] * got[1][i];
                lr += (double)got[0][i] * got[1][i];
            }

            const double corr = lr / sqrt(ll * rr);
            const double level = 10 * log10(ll / rr);

            okOrFail(fabs(corr) < 0.1 && fabs(level) < 1,
                     "delay::fdn: `out' and `out2' are the same tail, "
                     "uncorrelated",
                     "correlation " + num(corr) + ", " + num(level) +
                     " dB apart");
        }
    }

    /* ---- the same network twice ---- */

    /* Nothing in it is random, so two fresh synths render the same
       samples -- still or modulated -- and holding the lines still is
       not the same network as moving them. */
    {
        vector<float> a, b, c, d;
        string why;

        if (!render1(pluginPath, fdnGraph(1, 4, 6000, 0, 0, 0.7f), "fdn",
                     "out", 256, TH_DEFAULT_SAMPLES, a, why) ||
            !render1(pluginPath, fdnGraph(1, 4, 6000, 0, 0, 0.7f), "fdn",
                     "out", 256, TH_DEFAULT_SAMPLES, b, why) ||
            !render1(pluginPath, fdnGraph(1, 4, 6000, 12, 0.5f, 0.7f), "fdn",
                     "out", 256, TH_DEFAULT_SAMPLES, c, why) ||
            !render1(pluginPath, fdnGraph(1, 4, 6000, 12, 0.5f, 0.7f), "fdn",
                     "out", 256, TH_DEFAULT_SAMPLES, d, why))
            fail("delay::fdn renders", why);
        else
        {
            okOrFail(a.size() == b.size() &&
                     memcmp(&a[0], &b[0], a.size() * sizeof(float)) == 0 &&
                     c.size() == d.size() &&
                     memcmp(&c[0], &d[0], c.size() * sizeof(float)) == 0,
                     "delay::fdn: two fresh networks render the same "
                     "samples", "");
            okOrFail(memcmp(&a[0], &c[0], a.size() * sizeof(float)) != 0,
                     "delay::fdn: and `mod' moves them", "");
        }
    }

    /* ---- the shimmer puts the tail up an interval ---- */

    /* A third of a second of a 440 Hz sine into a network, and the
       spectrum of its tail from a second to two and a half. A reverb is
       linear and cannot make a frequency it was not given, so without
       the shimmer there is next to nothing at 880; with it the octave has
       to stand at least twenty decibels higher than that, and at a ratio
       of 1.5 it is 660 that has to. */
    {
        static const struct { float interval; double want; } cases[] = {
            { 2, 880 }, { 1.5f, 660 }
        };
        bool good = true;
        string detail;

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            double near[2];

            for (int on = 0; on < 2; on++)
            {
                vector<NodeSpec> spec =
                    fdnGraph(1, 8, 0, 0, 0, 0.7f, on ? 0.6f : 0,
                             cases[c].interval);
                vector<NodeSpec> tone =
                    shiftGraph(440, 1, 2204, 0);
                vector<float> got;
                string why;

                /* The sine, gated to its first third of a second by an
                   env::ad with no attack, then silence. */
                NodeSpec gate;

                gate.name = "gate";
                gate.spelling = "env/ad";

                Value a = { "a", 0 };
                Value d = { "d", TH_DEFAULT_SAMPLES / 3 };
                Value p = { "p", TH_MAX };
                Wire  am = { "amp", "gate", "out" };

                gate.values.push_back(a);
                gate.values.push_back(d);
                gate.values.push_back(p);

                tone[0].values.erase(tone[0].values.begin() + 1);
                tone[0].wires.push_back(am);

                spec[0] = tone[0];
                spec.insert(spec.begin(), gate);

                if (!render1(pluginPath, spec, "fdn", "out", 256,
                             (unsigned)(2.5 * TH_DEFAULT_SAMPLES), got, why))
                {
                    fail("delay::fdn renders", why);
                    return;
                }

                const vector<double> spectrum =
                    powerSpectrum(got, TH_DEFAULT_SAMPLES, 65536);

                near[on] = shareNear(spectrum, 65536, cases[c].want) *
                           energy(got, TH_DEFAULT_SAMPLES);
            }

            const double rise = 10 * log10(near[1] / (near[0] + 1e-30));

            if (!(rise > 20))
            {
                good = false;
                detail = "at " + num(cases[c].want) + " Hz the shimmer "
                         "added only " + num(rise) + " dB";
            }
        }

        okOrFail(good, "delay::fdn: `shimmer' puts the tail up by "
                       "`interval', where a plain network has nothing",
                 detail);
    }

    /* ---- and the climb stops ---- */

    /* Two seconds of noise into a minute-long network shimmering at the
       ceiling with no damping, which is the worst case: nothing but the
       shimmer's own gain holds the loop down. The tail twenty seconds on
       has to be quieter than it was two seconds on, not louder. It
       measures a third; without the shimmer it is an eighth, and before
       the high-pass it grew by six orders of magnitude, at DC. */
    {
        vector<NodeSpec> spec = fdnGraph(1, 60, 0, 12, 0.5f, 0.7f, 0.9f, 2);
        vector<float> got;
        string why;

        NodeSpec gate;

        gate.name = "gate";
        gate.spelling = "env/ad";

        Value a = { "a", 0 };
        Value d = { "d", 2 * TH_DEFAULT_SAMPLES };
        Value p = { "p", TH_MAX };

        gate.values.push_back(a);
        gate.values.push_back(d);
        gate.values.push_back(p);

        /* Gated by a multiply and not through `amp', which osc::noise
           reads a zero on as its default of full scale. */
        NodeSpec vca;

        vca.name = "vca";
        vca.spelling = "mixer/mul";

        Wire  g0 = { "in0", "src", "out" };
        Wire  g1 = { "in1", "gate", "out" };
        Wire  fed = { "in", "vca", "out" };

        vca.wires.push_back(g0);
        vca.wires.push_back(g1);

        spec[0].spelling = "osc/noise";
        spec[0].values.clear();
        spec[1].wires.clear();
        spec[1].wires.push_back(fed);
        spec.insert(spec.begin(), vca);
        spec.insert(spec.begin(), gate);

        if (!render1(pluginPath, spec, "fdn", "out", 256,
                     22 * TH_DEFAULT_SAMPLES, got, why))
            fail("delay::fdn renders", why);
        else
        {
            const double early = rms(vector<float>(
                got.begin() + 2 * TH_DEFAULT_SAMPLES,
                got.begin() + 4 * TH_DEFAULT_SAMPLES), 0);
            const double late = rms(got, 20 * TH_DEFAULT_SAMPLES);

            okOrFail(allFinite(got) && late < early,
                     "delay::fdn: a shimmer at the ceiling dies away "
                     "rather than climbing for ever",
                     "RMS " + num(early) + " two seconds on and " +
                     num(late) + " twenty seconds on");
        }
    }

    /* ---- finite at every corner ---- */

    /* A full-scale square wave rather than an impulse, so that the lines
       are as full as they get, at every combination of the ends of the
       args that set lengths and gains -- and a minute of decay at the
       smallest size, which is the most passes a second the node makes. */
    {
        static const float sizes[] = { 0, 0.1f, 4, 50 };
        static const float decays[] = { 0, 0.01f, 60, 1e6f };
        static const float damps[] = { 0, 20, 30000 };
        static const float mods[] = { 0, 64, 1000 };
        bool finite = true;
        string detail;

        for (float sz : sizes)
            for (float dc : decays)
                for (float dm : damps)
                    for (float md : mods)
                    {
                        vector<NodeSpec> spec =
                            fdnGraph(sz, dc, dm, md, 20, 0.9f, 1,
                                     md == 1000 ? 100 : 0.25f);
                        vector<float> got;
                        string why;

                        spec[0].spelling = "osc/simple";
                        spec[0].values.clear();

                        Value f = { "freq", 110 };
                        Value a = { "amp", TH_MAX };
                        Value w = { "waveform", 2 };

                        spec[0].values.push_back(f);
                        spec[0].values.push_back(a);
                        spec[0].values.push_back(w);

                        if (!render1(pluginPath, spec, "fdn", "out", 256,
                                     TH_DEFAULT_SAMPLES / 2, got, why))
                        {
                            fail("delay::fdn renders", why);
                            return;
                        }

                        if (finite && !allFinite(got))
                        {
                            finite = false;
                            detail = "size " + num(sz) + ", decay " +
                                     num(dc) + ", damping " + num(dm) +
                                     ", mod " + num(md);
                        }
                    }

        okOrFail(finite, "delay::fdn: finite at every corner, shimmering at "
                         "the ceiling", detail);
    }

    windowsAgree(pluginPath, fdnGraph(1, 4, 6000, 12, 0.5f, 0.7f), "fdn",
                 "out", "delay::fdn: the same tail at one sample a window and "
                 "at five hundred");
}

/* ---- osc::fmop ---------------------------------------------------------- */

/* What the node claims, and why each one is here rather than left to a
   reading of the file:
 *
 *   - with `index' at zero it is a sine at `freq' times `ratio', which is
 *     the thing everything else is measured against;
 *   - a constant on `mod' moves the wave and not its pitch, which is the
 *     whole difference between this node and `osc::simple's `fm' and the
 *     reason a DX patch can be written on it at all;
 *   - two operators at 1:1 with an index of one put their partials where
 *     Bessel says, which is the claim that `index' is radians of phase
 *     and not a number of samples;
 *   - and the same index at two pitches is the same timbre, which
 *     follows from that and is what a patch is;
 *   - `feedback' at 1 leans the sine toward a sawtooth;
 *   - `reset' is a hard sync: the output repeats at the resetting rate
 *     and not at its own;
 *   - and a window boundary is not an event.
 */

/* One operator, free-running, with `mod' held at a constant. */
static vector<NodeSpec> fmopGraph (float hz, float ratio, float index,
                                   float mod, float feedback)
{
    vector<NodeSpec> spec;
    NodeSpec op;

    op.name = "op";
    op.spelling = "osc/fmop";

    Value f = { "freq", hz };
    Value r = { "ratio", ratio };
    Value x = { "index", index };
    Value m = { "mod", mod };
    Value b = { "feedback", feedback };

    op.values.push_back(f);
    op.values.push_back(r);
    op.values.push_back(x);
    op.values.push_back(m);
    op.values.push_back(b);

    spec.push_back(op);

    return spec;
}

/* Two of them, the modulator into the carrier: the whole of a two-op DX
   patch, and the smallest graph that is not a sine. */
static vector<NodeSpec> fmopPairGraph (float hz, float modRatio, float index)
{
    vector<NodeSpec> spec;
    NodeSpec m, c;

    m.name = "m";
    m.spelling = "osc/fmop";

    Value mf = { "freq", hz };
    Value mr = { "ratio", modRatio };

    m.values.push_back(mf);
    m.values.push_back(mr);

    c.name = "op";
    c.spelling = "osc/fmop";

    Value cf = { "freq", hz };
    Value cr = { "ratio", 1 };
    Value cx = { "index", index };
    Wire  cm = { "mod", "m", "out" };

    c.values.push_back(cf);
    c.values.push_back(cr);
    c.values.push_back(cx);
    c.wires.push_back(cm);

    spec.push_back(m);
    spec.push_back(c);

    return spec;
}

/* An operator whose `reset' is another oscillator's `sync': hard sync,
   where the master's rate is the one the ear hears and the slave's is
   the formant sitting on top of it. */
static vector<NodeSpec> fmopSyncGraph (float master, float hz)
{
    vector<NodeSpec> spec = fmopGraph(hz, 1, 0, 0, 0);
    NodeSpec src;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", master };
    Value a = { "amp", TH_MAX };
    Value w = { "waveform", 0 };

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    Wire reset = { "reset", "src", "sync" };

    spec[0].wires.push_back(reset);
    spec.insert(spec.begin(), src);

    return spec;
}

static void checkFmop (const string &pluginPath)
{
    /* 441 Hz over a window of exactly one second: every partial measured
       below is a whole number of cycles in the window, so each lands on a
       bin center and nothing leaks into its neighbors. A quarter of a
       second discarded in front, which is longer than anything here takes
       to settle. */
    const double f0 = 441;
    const unsigned window = TH_DEFAULT_SAMPLES;
    const size_t from = TH_DEFAULT_SAMPLES / 4;
    const unsigned len = window + (unsigned)from;

    /* ---- index 0 is a sine at freq * ratio ---- */

    {
        static const float ratios[] = { 0.5f, 1, 2, 3.5f };
        bool good = true;
        string detail;

        for (size_t c = 0; c < sizeof(ratios) / sizeof(ratios[0]) && good; c++)
        {
            vector<float> out;
            string why;

            if (!render1(pluginPath, fmopGraph((float)f0, ratios[c], 0, 0, 0),
                         "op", "out", 256, len, out, why))
            {
                fail("osc::fmop renders", why);
                return;
            }

            const double at = bin(out, from, window, f0 * ratios[c]);
            const double next = bin(out, from, window, 2 * f0 * ratios[c]);

            if (!(fabs(at - TH_MAX) < 0.01 && next < 0.01))
            {
                good = false;
                detail = "at ratio " + num(ratios[c]) + ": " + num(at) +
                         " at the fundamental, " + num(next) + " at twice it";
            }
        }

        okOrFail(good, "osc::fmop: `index = 0' is a full-scale sine at "
                       "`freq' times `ratio'", detail);
    }

    /* ---- a constant on `mod' does not detune it ---- */

    /* The node's reason for existing. `osc::simple's `fm' adds to the
       phase increment, so a modulator sitting at any DC at all is a
       permanent change of frequency; this adds to the phase, where a
       constant is a head start and nothing more. Measured at an index of
       three, which on the other reading would be a wild mistuning. */
    {
        vector<float> out;
        string why;

        if (!render1(pluginPath, fmopGraph((float)f0, 1, 3, TH_MAX, 0),
                     "op", "out", 256, len, out, why))
            fail("osc::fmop renders", why);
        else
        {
            const double at = bin(out, from, window, f0);
            const double next = bin(out, from, window, 2 * f0);

            okOrFail(fabs(at - TH_MAX) < 0.01 && next < 0.01,
                     "osc::fmop: a `mod' that does not move is a phase "
                     "offset, not a pitch",
                     num(at) + " at the fundamental, " + num(next) +
                     " at twice it");
        }
    }

    /* ---- a 1:1 pair at index 1 is where Bessel says ---- */

    /* sin(x + I sin x) is sum over k of J_k(I) sin((1+k)x), and the terms
       below the fundamental fold back on top of the ones above it, so the
       nth harmonic comes out at J_{n-1}(I) + (-1)^n J_{n+1}(I). At I = 1
       that is the four numbers below, and they are the statement that
       `index' is radians: a node that took a number of samples instead
       would put its sidebands somewhere else at every pitch but one. */
    {
        /* J_0(1) through J_5(1). */
        static const double J[] = { 0.7651977, 0.4400506, 0.1149035,
                                    0.0195634, 0.0024766, 0.0002498 };
        static const double want[] = { J[0] - J[2], J[1] + J[3],
                                       J[2] - J[4], J[3] + J[5] };

        vector<float> out;
        string why;

        if (!render1(pluginPath, fmopPairGraph((float)f0, 1, 1),
                     "op", "out", 256, len, out, why))
            fail("osc::fmop renders", why);
        else
        {
            bool good = true;
            string detail;

            for (int n = 1; n <= 4 && good; n++)
            {
                const double got = bin(out, from, window, f0 * n);

                if (fabs(got - want[n - 1]) > 0.005)
                {
                    good = false;
                    detail = "partial " + num(n) + " was " + num(got) +
                             ", Bessel says " + num(want[n - 1]);
                }
            }

            okOrFail(good, "osc::fmop: a 1:1 pair at `index = 1' has the "
                           "partials Bessel gives it", detail);
        }
    }

    /* ---- and the same index is the same timbre two octaves down ---- */

    /* What `index' being a ratio of the cycle buys: a patch is a patch
       and not a note. Two octaves is far enough that a deviation fixed
       in samples would be four times the index here and audibly a
       different instrument. 110.25 Hz is f0 / 4, so its partials still
       land on bin centers. */
    {
        vector<float> high, low;
        string why;

        if (!render1(pluginPath, fmopPairGraph((float)f0, 1, 1),
                     "op", "out", 256, len, high, why) ||
            !render1(pluginPath, fmopPairGraph((float)f0 / 4, 1, 1),
                     "op", "out", 256, len, low, why))
            fail("osc::fmop renders", why);
        else
        {
            bool good = true;
            string detail;

            for (int n = 2; n <= 4 && good; n++)
            {
                const double a = bin(high, from, window, f0 * n) /
                                 bin(high, from, window, f0);
                const double b = bin(low, from, window, f0 / 4 * n) /
                                 bin(low, from, window, f0 / 4);

                if (fabs(a - b) > 0.01)
                {
                    good = false;
                    detail = "partial " + num(n) + " is " + num(a) +
                             " of the fundamental at 441 Hz and " + num(b) +
                             " two octaves down";
                }
            }

            okOrFail(good, "osc::fmop: the same `index' is the same timbre "
                           "at every pitch", detail);
        }
    }

    /* ---- feedback leans the sine toward a saw ---- */

    /* y = sin(x + b*y) has harmonics 2*J_n(n*b)/(n*b), which at b = 1 is
       0.88, 0.353, 0.206, 0.141 -- a fundamental with a tail falling off
       a little faster than a sawtooth's 1, 1/2, 1/3, 1/4. The node runs
       that recurrence a sample late and through a two-sample average,
       which costs the tail a little of its height -- 0.861, 0.327, 0.177,
       0.111 as measured here -- so what is asserted is the shape rather
       than the four numbers: a fundamental, three partials under it in
       order, and the second one between a quarter and a half of the
       first. At `feedback = 0' there is
       nothing above the fundamental at all, which is the other half of
       the claim. */
    {
        vector<float> plain, fed;
        string why;

        if (!render1(pluginPath, fmopGraph((float)f0, 1, 0, 0, 0),
                     "op", "out", 256, len, plain, why) ||
            !render1(pluginPath, fmopGraph((float)f0, 1, 0, 0, 1),
                     "op", "out", 256, len, fed, why))
            fail("osc::fmop renders", why);
        else
        {
            const double h1 = bin(fed, from, window, f0);
            const double h2 = bin(fed, from, window, 2 * f0);
            const double h3 = bin(fed, from, window, 3 * f0);
            const double h4 = bin(fed, from, window, 4 * f0);

            okOrFail(bin(plain, from, window, 2 * f0) < 0.01 &&
                     h1 > h2 && h2 > h3 && h3 > h4 &&
                     h2 / h1 > 0.25 && h2 / h1 < 0.55 &&
                     h3 / h1 > 0.10 && h3 / h1 < 0.35,
                     "osc::fmop: `feedback = 1' leans the sine toward a "
                     "sawtooth",
                     "partials " + num(h1) + ", " + num(h2) + ", " +
                     num(h3) + ", " + num(h4));
        }
    }

    /* ---- `reset' is a hard sync ---- */

    /* Driven from another oscillator's `sync', the operator starts its
       cycle again on the master's period whatever its own frequency is,
       so what comes out repeats at the master's rate and not at its own.
       630 against 100 shares no factor worth the name, so a slave that
       ignored the reset would not repeat at 441 samples by accident. */
    {
        vector<float> out;
        string why;
        const unsigned period = TH_DEFAULT_SAMPLES / 100;

        if (!render1(pluginPath, fmopSyncGraph(100, 630), "op", "out", 256,
                     len, out, why))
            fail("osc::fmop renders", why);
        else
        {
            bool same = true;
            string detail;

            for (size_t i = from; i + period < out.size() && same; i++)
                if (fabs(out[i + period] - out[i]) > TH_MAX * 1e-6)
                {
                    same = false;
                    detail = "sample " + num((double)i) + ": " +
                             num(out[i]) + " against " + num(out[i + period]) +
                             " a master cycle later";
                }

            okOrFail(same, "osc::fmop: a `reset' from another oscillator's "
                           "`sync' makes the output repeat at the master's "
                           "rate", detail);
        }
    }

    /* ---- the declared range is the slider's and not a law ---- */

    /* `index' carries a range so that a knob knows how far to travel,
       and the tree's rule is that such a range is advice: a graph may
       write past it and gets what it asked for. Clamping at the top
       instead made every index above twenty the same sound, which is a
       patch quietly turned into a different patch -- and bought nothing,
       since `mod' is unbounded and the deviation is the product of the
       two.

       Measured as how far up the spectrum the sidebands get, rather
       than as "not equal": a 1:1 pair's reach past the carrier goes
       with its index, so a ceiling on the index is a ceiling on the
       reach, and a clamp shows up as the two spectra stopping in the
       same place. Which is exactly what it did: 26 partials either
       way. */
    {
        vector<float> at20, at60;
        string why;

        if (!render1(pluginPath, fmopPairGraph((float)f0, 1, 20),
                     "op", "out", 256, len, at20, why) ||
            !render1(pluginPath, fmopPairGraph((float)f0, 1, 60),
                     "op", "out", 256, len, at60, why))
            fail("osc::fmop renders", why);
        else
        {
            int reach20 = 0, reach60 = 0;

            for (int n = 1; n * f0 < 20000; n++)
            {
                if (bin(at20, from, window, f0 * n) > 0.01)
                    reach20 = n;

                if (bin(at60, from, window, f0 * n) > 0.01)
                    reach60 = n;
            }

            okOrFail(reach60 > reach20 + 10,
                     "osc::fmop: an `index' past the declared range is not "
                     "clamped to it",
                     "index 20 reaches partial " + num(reach20) +
                     " and index 60 reaches " + num(reach60));
        }
    }

    /* ---- a negative index is none, and a NaN is none ---- */

    /* The floor that is still enforced, and the only part of it that is:
       both have to land somewhere, and `no modulation' is the reading
       that leaves a sine rather than a surprise. */
    {
        static const float bad[] = { -4, std::numeric_limits<float>::quiet_NaN() };
        bool good = true;
        string detail;

        for (size_t c = 0; c < sizeof(bad) / sizeof(bad[0]) && good; c++)
        {
            vector<float> out;
            string why;

            if (!render1(pluginPath, fmopPairGraph((float)f0, 1, bad[c]),
                         "op", "out", 256, len, out, why))
            {
                fail("osc::fmop renders", why);
                return;
            }

            const double at = bin(out, from, window, f0);
            const double next = bin(out, from, window, 2 * f0);

            if (!(fabs(at - TH_MAX) < 0.01 && next < 0.01))
            {
                good = false;
                detail = "index " + num(bad[c]) + ": " + num(at) +
                         " at the fundamental, " + num(next) +
                         " at twice it";
            }
        }

        okOrFail(good, "osc::fmop: an `index' below zero, and one that is "
                       "not a number, are both no modulation", detail);
    }

    windowsAgree(pluginPath, fmopPairGraph((float)f0, 3.5f, 4), "op", "out",
                 "osc::fmop: the same pair at one sample a window and at "
                 "five hundred");

    /* And again with the feedback loop running, which is the only state
       this node carries besides the phase: `y1' and `y2' cross a window
       boundary the same way, and a pair that agreed without them would
       not have said so. */
    windowsAgree(pluginPath, fmopGraph((float)f0, 1, 0, 0, 0.9f), "op",
                 "out", "osc::fmop: the same operator, fed back, at one "
                 "sample a window and at five hundred");
}

/* ---- osc::simple -------------------------------------------------------- */

/* The phase counter, which is the state, and the one property of it that is
 * not visible in a render of the corpus: it has to stay inside the cycle
 * when the *cycle* is what moved.
 *
 * `position' advances by a sample at a time, so one subtraction is all a
 * wrap normally needs, and that is what the plugin did -- once, per sample.
 * The other way a phase leaves its range is `wavelength' shrinking under it,
 * and there are two of those. A note retuned upwards shortens the wavelength
 * by the interval, so a mono channel with no lag in its graph hands the
 * oscillator a phase several cycles long; and `fmamt' is a number of samples
 * the phase jumps per sample, so a large one carries it past a whole cycle
 * in one step. Either way `ratio' below arrived at ten or fifteen rather
 * than at one, and every waveform but the sine ran off its scale until the
 * subtractions caught up a cycle at a time -- roughly twenty-nine times full
 * scale for four samples on a bare saw retuned from C1 to C5, which the
 * output clamp turned into a click.
 *
 * Both are measured on the sawtooth. The sine is bounded by sin() whatever
 * the phase is, which is exactly why this could not be found by listening to
 * the node that is usually wired.
 */

/* A saw whose `freq' steps between two pitches, an octave and a half apart,
   forty times a second: a square through env::map, which is the smallest
   step generator these graphs can hold. */
static vector<NodeSpec> simpleStepGraph (float low, float high)
{
    vector<NodeSpec> spec;
    NodeSpec sq, map, osc;

    sq.name = "sq";
    sq.spelling = "osc/simple";

    Value sqf = { "freq", 40 };
    Value sqw = { "waveform", 2 };
    Value sqa = { "amp", 1 };

    sq.values.push_back(sqf);
    sq.values.push_back(sqw);
    sq.values.push_back(sqa);

    map.name = "map";
    map.spelling = "env/map";

    Value mi = { "inmin", -1 };
    Value ma = { "inmax", 1 };
    Value mo = { "outmin", low };
    Value mx = { "outmax", high };
    Wire  mw = { "in", "sq", "out" };

    map.values.push_back(mi);
    map.values.push_back(ma);
    map.values.push_back(mo);
    map.values.push_back(mx);
    map.wires.push_back(mw);

    osc.name = "osc";
    osc.spelling = "osc/simple";

    Value ow = { "waveform", 1 };
    Value oa = { "amp", 1 };
    Wire  of = { "freq", "map", "out" };

    osc.values.push_back(ow);
    osc.values.push_back(oa);
    osc.wires.push_back(of);

    spec.push_back(sq);
    spec.push_back(map);
    spec.push_back(osc);

    return spec;
}

/* The same saw, at one pitch, with `fmamt' samples of phase per sample
   coming from a sine -- the other way to carry the phase past a cycle. */
static vector<NodeSpec> simpleFmGraph (float hz, float fmamt)
{
    vector<NodeSpec> spec;
    NodeSpec mod, osc;

    mod.name = "mod";
    mod.spelling = "osc/simple";

    Value mf = { "freq", 3 };
    Value mw = { "waveform", 0 };
    Value mamp = { "amp", 1 };

    mod.values.push_back(mf);
    mod.values.push_back(mw);
    mod.values.push_back(mamp);

    osc.name = "osc";
    osc.spelling = "osc/simple";

    Value of = { "freq", hz };
    Value ow = { "waveform", 1 };
    Value oa = { "amp", 1 };
    Value ox = { "fmamt", fmamt };
    Wire  om = { "fm", "mod", "out" };

    osc.values.push_back(of);
    osc.values.push_back(ow);
    osc.values.push_back(oa);
    osc.values.push_back(ox);
    osc.wires.push_back(om);

    spec.push_back(mod);
    spec.push_back(osc);

    return spec;
}

static void checkSimple (const string &pluginPath)
{
    /* C1 and C5, which is the jump the bug report was made on. */
    const vector<NodeSpec> stepped = simpleStepGraph(32.7032f, 523.2511f);

    vector<float> got;
    string why;

    if (!render1(pluginPath, stepped, "osc", "out", 512, 44100, got, why))
    {
        fail("osc::simple: a saw whose pitch steps renders", why);
    }
    else
    {
        /* `amp' is 1, the arg declares its range as -1..1, and a sawtooth
           reaches both ends of it every cycle. Anything past that is phase
           that was not brought back inside the cycle. */
        okOrFail(allFinite(got) && peak(got, 0) <= 1.0001,
                 "osc::simple: a pitch that steps up an octave and a half "
                 "leaves the phase inside the cycle",
                 "peak " + num(peak(got, 0)));
    }

    /* Fifty cycles of phase in one sample at the pitch below, which is more
       than any number of single subtractions inside one sample can undo. */
    const vector<NodeSpec> modulated = simpleFmGraph(440, 5000);

    if (!render1(pluginPath, modulated, "osc", "out", 512, 44100, got, why))
    {
        fail("osc::simple: a saw under deep FM renders", why);
    }
    else
    {
        okOrFail(allFinite(got) && peak(got, 0) <= 1.0001,
                 "osc::simple: an `fm' input that carries the phase past a "
                 "whole cycle leaves it inside the cycle",
                 "peak " + num(peak(got, 0)));
    }

    windowsAgree(pluginPath, stepped, "osc", "out",
                 "osc::simple: the same stepping saw at one sample a window "
                 "and at five hundred");
}

/* ---- osc::sample -------------------------------------------------------- */

/* The node that plays a file, and the only one here whose input is not a
 * number. What is claimed:
 *
 *   - at `freq = root' a file comes out frame for frame, exactly, with no
 *     interpolation happening at all -- which is the property a drum
 *     machine rests on and the one that catches an off-by-one in the
 *     playhead;
 *   - at twice `freq' it is half as long, which is what makes `root' a
 *     root note rather than a rate;
 *   - `play' is 1 for exactly as long as there is file and 0 after, so a
 *     graph can wire it straight to its io node and have the note be the
 *     sample's own length;
 *   - `loop' takes the last N frames again, forever, and the wrap lands
 *     on a frame rather than between two;
 *   - a file that is not there is silence and not a crash;
 *   - and a window boundary is not an event.
 *
 * The file is written here rather than shipped: what these want is a ramp
 * whose every frame is known in advance, and a ramp is not a sound
 * anybody would put in a kit.
 */

/* Sixteen-bit mono RIFF, little-endian, assembled byte by byte -- the
   same discipline the reader in plugins/osc/sampleslot.h keeps, and for
   the same reason: a struct write here would be the one place this
   harness stopped being true on a big-endian host. */
/* A wav whose header says something the file does not back up: `declared'
 * bytes in the `data' chunk however many frames actually follow, and
 * whatever `rate' is asked for however absurd.
 *
 * Both numbers size an allocation in the reader, and neither is bounded by
 * anything in the file, so both are worth a case. A `data' length of
 * 0xfffffff0 on a two-hundred-byte file used to reserve 8.6 GB -- which a
 * host with overcommit hands over and a 32-bit emscripten heap answers
 * with an uncaught bad_alloc -- and a sample rate of 1 turns a hundred
 * frames into a hundred seconds of audio on the way in.
 */
static bool writeOddWav (const string &path, size_t frames,
                         unsigned long declared, unsigned rate);

static bool writeWav (const string &path, const vector<float> &frames,
                      unsigned rate)
{
    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return false;

    const unsigned long bytes = (unsigned long)frames.size() * 2;
    unsigned char h[44];
    size_t at = 0;

    struct P {
        static void u32 (unsigned char *p, unsigned long v) {
            p[0] = (unsigned char)(v & 0xff);
            p[1] = (unsigned char)((v >> 8) & 0xff);
            p[2] = (unsigned char)((v >> 16) & 0xff);
            p[3] = (unsigned char)((v >> 24) & 0xff);
        }
        static void u16 (unsigned char *p, unsigned v) {
            p[0] = (unsigned char)(v & 0xff);
            p[1] = (unsigned char)((v >> 8) & 0xff);
        }
    };

    memcpy(h + at, "RIFF", 4);              at += 4;
    P::u32(h + at, 36 + bytes);             at += 4;
    memcpy(h + at, "WAVE", 4);              at += 4;
    memcpy(h + at, "fmt ", 4);              at += 4;
    P::u32(h + at, 16);                     at += 4;
    P::u16(h + at, 1);                      at += 2;   /* PCM      */
    P::u16(h + at, 1);                      at += 2;   /* mono     */
    P::u32(h + at, rate);                   at += 4;
    P::u32(h + at, rate * 2);               at += 4;
    P::u16(h + at, 2);                      at += 2;   /* align    */
    P::u16(h + at, 16);                     at += 2;   /* bits     */
    memcpy(h + at, "data", 4);              at += 4;
    P::u32(h + at, bytes);                  at += 4;

    fwrite(h, 1, at, f);

    for (size_t i = 0; i < frames.size(); i++)
    {
        double v = frames[i] * 32767.0;
        int q = (int)((v < 0) ? v - 0.5 : v + 0.5);
        unsigned char b[2];

        if (q > 32767)  q = 32767;
        if (q < -32768) q = -32768;

        P::u16(b, (unsigned)(q & 0xffff));
        fwrite(b, 1, 2, f);
    }

    fclose(f);

    return true;
}

/* The ramp every case below plays: RAMP_LEN frames, frame i holding
   i / RAMP_LEN, so a sample's value *is* its index and a comparison says
   which frame came out rather than merely that something did. */
#define RAMP_LEN 1000

/* See the declaration above. Deliberately not writeWav with arguments:
   what this writes is a header that is wrong, and a writer that can do
   both is a writer somebody edits the wrong half of. */
static bool writeOddWav (const string &path, size_t frames,
                         unsigned long declared, unsigned rate)
{
    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return false;

    const unsigned long bytes = (unsigned long)frames * 2;
    unsigned char h[44];
    size_t at = 0;

    struct P {
        static void u32 (unsigned char *p, unsigned long v) {
            p[0] = (unsigned char)(v & 0xff);
            p[1] = (unsigned char)((v >> 8) & 0xff);
            p[2] = (unsigned char)((v >> 16) & 0xff);
            p[3] = (unsigned char)((v >> 24) & 0xff);
        }
        static void u16 (unsigned char *p, unsigned v) {
            p[0] = (unsigned char)(v & 0xff);
            p[1] = (unsigned char)((v >> 8) & 0xff);
        }
    };

    memcpy(h + at, "RIFF", 4);              at += 4;
    P::u32(h + at, 36 + bytes);             at += 4;
    memcpy(h + at, "WAVE", 4);              at += 4;
    memcpy(h + at, "fmt ", 4);              at += 4;
    P::u32(h + at, 16);                     at += 4;
    P::u16(h + at, 1);                      at += 2;   /* PCM      */
    P::u16(h + at, 1);                      at += 2;   /* mono     */
    P::u32(h + at, rate);                   at += 4;
    P::u32(h + at, rate * 2);               at += 4;
    P::u16(h + at, 2);                      at += 2;   /* align    */
    P::u16(h + at, 16);                     at += 2;   /* bits     */
    memcpy(h + at, "data", 4);              at += 4;
    /* The lie, or the truth when the caller passes 0. */
    P::u32(h + at, declared ? declared : bytes);   at += 4;

    fwrite(h, 1, at, f);

    for (size_t i = 0; i < frames; i++)
    {
        /* Something audible, so that a file the reader *should* play is
           not confused with one it refused. */
        const unsigned char b[2] = { 0x00, 0x10 };

        fwrite(b, 1, 2, f);
    }

    return fclose(f) == 0;
}

static vector<NodeSpec> sampleGraph (const char *file, float freq, float root,
                                     float loop, float start)
{
    vector<NodeSpec> spec;
    NodeSpec smp;

    smp.name = "smp";
    smp.spelling = "osc/sample";

    Text f = { "file", file };
    Value fr = { "freq", freq };
    Value rt = { "root", root };
    Value lp = { "loop", loop };
    Value st = { "start", start };

    smp.texts.push_back(f);
    smp.values.push_back(fr);
    smp.values.push_back(rt);
    smp.values.push_back(lp);
    smp.values.push_back(st);

    spec.push_back(smp);

    return spec;
}

static void checkSample (const string &pluginPath)
{
    /* A directory of its own, with the `samples/' the node looks under,
       and THINK_DSP_PATH pointed at it -- which is the same search a
       .dsp goes through, so this exercises the lookup and not a path
       the harness handed over. */
    const string dir = thUtil::tempFile("statecheck-samples-");

    if (dir.empty())
    {
        fail("osc::sample: could not make a scratch directory", "");
        return;
    }

    std::filesystem::remove(dir);

    std::error_code ec;

    std::filesystem::create_directories(dir + "/samples", ec);

    if (ec)
    {
        fail("osc::sample: could not make a scratch directory", ec.message());
        return;
    }

    vector<float> ramp(RAMP_LEN), want(RAMP_LEN);

    for (int i = 0; i < RAMP_LEN; i++)
    {
        ramp[i] = (float)i / RAMP_LEN;

        /* What the file will hand back, which is not quite what went in:
           a 16-bit wav is written by multiplying by 32767 and rounding,
           and read by dividing by 32768. Comparing against the round
           trip rather than against the original lets every check below
           be exact -- a tolerance wide enough to swallow a quantization
           step is also wide enough to swallow a playhead half a frame
           out, which is the mistake these exist to catch. */
        want[i] = (float)((double)(int)(ramp[i] * 32767.0 + 0.5) / 32768.0);
    }

    if (!writeWav(dir + "/samples/ramp.wav", ramp, TH_DEFAULT_SAMPLES))
    {
        fail("osc::sample: could not write the scratch wav", "");
        std::filesystem::remove_all(dir, ec);
        return;
    }

    /* Three known ramps with different slopes; frame 16 separates them. */
    const float levels[] = { 0.0625f, 0.125f, 0.1875f };
    const char *layers[] = { "layer1.wav", "layer2.wav", "layer3.wav" };

    for (unsigned k = 0; k < 3; k++)
    {
        vector<float> layerRamp(64);

        for (unsigned i = 0; i < layerRamp.size(); i++)
            layerRamp[i] = (float)i / 64 * (float)(k + 1) * 0.25f;

        if (!writeWav(dir + "/samples/" + layers[k], layerRamp,
                      TH_DEFAULT_SAMPLES))
        {
            fail("osc::sample: could not write a layer wav", layers[k]);
            std::filesystem::remove_all(dir, ec);
            return;
        }
    }

#ifdef _WIN32
    _putenv_s("THINK_DSP_PATH", dir.c_str());
#else
    setenv("THINK_DSP_PATH", dir.c_str(), 1);
#endif

    /* ---- frame for frame at freq = root ---- */

    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "smp", "out" };
        Watch w1 = { "smp", "play" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, sampleGraph("ramp.wav", 440, 440, 0, 0),
                    watch, 256, RAMP_LEN + 500, got, why))
            fail("osc::sample renders", why);
        else
        {
            bool same = true;
            string detail;

            for (int i = 0; i < RAMP_LEN && same; i++)
                if (got[0][i] != want[i])
                {
                    same = false;
                    detail = "frame " + num((double)i) + ": " +
                             num(got[0][i]) + " against " + num(want[i]);
                }

            okOrFail(same, "osc::sample: at `freq = root' the file comes out "
                           "frame for frame", detail);

            /* ---- and `play' is the file's own length ---- */

            bool held = true;

            for (int i = 0; i < RAMP_LEN && held; i++)
                if (got[1][i] != 1)
                {
                    held = false;
                    detail = "`play' was " + num(got[1][i]) + " at frame " +
                             num((double)i) + ", inside the file";
                }

            for (size_t i = RAMP_LEN; i < got[1].size() && held; i++)
                if (got[1][i] != 0)
                {
                    held = false;
                    detail = "`play' was " + num(got[1][i]) + " at frame " +
                             num((double)i) + ", past the end";
                }

            okOrFail(held, "osc::sample: `play' is 1 for exactly the frames "
                           "the file has", detail);
        }
    }

    /* ---- an octave up is half as long ---- */

    /* Which is the whole of what `root' means: the file is read at
       freq/root frames a sample, so 880 against a root of 440 takes half
       the time and every other frame comes out. */
    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "smp", "out" };
        Watch w1 = { "smp", "play" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, sampleGraph("ramp.wav", 880, 440, 0, 0),
                    watch, 256, RAMP_LEN, got, why))
            fail("osc::sample renders", why);
        else
        {
            bool same = true;
            string detail;

            for (int i = 0; i < RAMP_LEN / 2 && same; i++)
                if (got[0][i] != want[i * 2])
                {
                    same = false;
                    detail = "frame " + num((double)i) + ": " +
                             num(got[0][i]) + " against frame " +
                             num((double)(i * 2)) + "'s " + num(want[i * 2]);
                }

            okOrFail(same && got[1][RAMP_LEN / 2 + 2] == 0,
                     "osc::sample: at twice `root' it is half as long",
                     detail.empty() ? "`play' did not drop at the halfway "
                                      "mark" : detail);
        }
    }

    /* ---- the loop takes the last frames again ---- */

    /* `loop = 200' means the last two hundred frames repeat, so after
       frame 999 the playhead is at 800 and the output is the ramp's top
       fifth over and over. Landing on a frame rather than between two is
       what stops the wrap clicking, and a ramp is the signal that shows
       it: any error puts a step in a straight line. */
    {
        vector<float> out;
        string why;
        const int loop = 200;

        if (!render1(pluginPath,
                     sampleGraph("ramp.wav", 440, 440, (float)loop, 0),
                     "smp", "out", 256, RAMP_LEN * 3, out, why))
            fail("osc::sample renders", why);
        else
        {
            bool wraps = true;
            string detail;

            for (size_t i = RAMP_LEN; i < out.size() && wraps; i++)
            {
                const int at = RAMP_LEN - loop +
                               (int)((i - RAMP_LEN) % (size_t)loop);

                if (out[i] != want[at])
                {
                    wraps = false;
                    detail = "frame " + num((double)i) + ": " +
                             num(out[i]) + " against frame " +
                             num((double)at) + "'s " + num(want[at]);
                }
            }

            okOrFail(wraps, "osc::sample: `loop' takes the last frames again, "
                            "landing on one", detail);
        }
    }

    /* ---- and a file that is not there is silence ---- */

    {
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        Watch w0 = { "smp", "out" };
        Watch w1 = { "smp", "play" };

        watch.push_back(w0);
        watch.push_back(w1);

        if (!render(pluginPath, sampleGraph("nosuchthing.wav", 440, 440, 0, 0),
                    watch, 256, 2000, got, why))
            fail("osc::sample renders", why);
        else
        {
            bool quiet = true;

            for (size_t i = 0; i < got[0].size() && quiet; i++)
                if (got[0][i] != 0 || got[1][i] != 0)
                    quiet = false;

            okOrFail(quiet, "osc::sample: a file that is not there is silence "
                            "with `play' down, not a crash", "");
        }
    }

    /* ---- a header that says more than the file holds ---- */

    /* Every one of these is silence rather than a crash, which is the same
       promise the missing file above makes. They are rendered rather than
       reasoned about because what they are guarding is an allocation: the
       sizes come out of the header, and the only way to know a bound is
       enforced is to hand it a number past the bound. */
    {
        static const struct { const char *file; const char *what;
                              size_t frames; unsigned long declared;
                              unsigned rate; bool plays; } odd[] = {
            /* Claims ~4 GB of data and holds four hundred bytes. Read: the
               frames that are there are real and get played, and only the
               length field was nonsense. */
            { "hugelen.wav",  "a `data' length past the end of the file",
              200, 0xfffffff0UL, TH_DEFAULT_SAMPLES, true },
            /* Claims twice the frames it has: the honest truncation, which
               is a thing a half-written file really is. Read, for the same
               reason. */
            { "trunc.wav",    "a `data' length twice the frames there are",
              200, 800UL, TH_DEFAULT_SAMPLES, true },
            /* One hertz: resampling to the synth's rate asks for forty
               thousand times as many frames as the file has, which is past
               what the playhead can address. Refused. */
            { "slowrate.wav", "a sample rate of 1 Hz",
              1000, 0, 1, false },
        };

        for (size_t c = 0; c < sizeof(odd) / sizeof(odd[0]); c++)
        {
            vector<Watch> watch;
            vector< vector<float> > got;
            string why;

            Watch w0 = { "smp", "out" };
            Watch w1 = { "smp", "play" };

            watch.push_back(w0);
            watch.push_back(w1);

            if (!writeOddWav(dir + "/samples/" + odd[c].file,
                             odd[c].frames, odd[c].declared, odd[c].rate))
            {
                fail("osc::sample: could not write the scratch wav", "");
                continue;
            }

            if (!render(pluginPath, sampleGraph(odd[c].file, 440, 440, 0, 0),
                        watch, 256, 2000, got, why))
            {
                fail(string("osc::sample: ") + odd[c].what +
                     " brought the render down", why);
                continue;
            }

            /* Which of the two answers, and not merely "it did not
               crash": a host that overcommits hands over the 8.6 GB the
               bad length asks for and carries on, so a case that only
               asserted survival would pass on the code that had no bound
               at all. `hugelen' and `trunc' hold real frames and have to
               come out sounding; the length that cannot be played has to
               come out silent. */
            bool sounded = false, finite = true;

            for (size_t i = 0; i < got[0].size(); i++)
            {
                if (!thIsFinite(got[0][i]) || !thIsFinite(got[1][i]))
                    finite = false;

                if (got[0][i] != 0 || got[1][i] != 0)
                    sounded = true;
            }

            okOrFail(finite && sounded == odd[c].plays,
                     string("osc::sample: ") + odd[c].what +
                     (odd[c].plays ? " plays the frames that are there"
                                   : " is refused, and is silence"),
                     finite ? (sounded ? "it sounded" : "it was silent")
                            : "a sample was not a number");
        }
    }

    /* ---- velocity boundaries, fallback, and one choice per hit ---- */

    {
        static const float selects[] = { 0, 0.33334f, 0.66667f, 1 };
        static const unsigned chosen[] = { 0, 1, 2, 2 };
        bool good = true;
        string detail;

        for (unsigned k = 0; k < 4 && good; k++)
        {
            vector<NodeSpec> spec = sampleGraph(layers[0], 440, 440, 0, 16);
            spec[0].texts.push_back(Text{ "file2", layers[1] });
            spec[0].texts.push_back(Text{ "file3", layers[2] });
            spec[0].values.push_back(Value{ "select", selects[k] });
            spec[0].values.push_back(Value{ "trigger", 1 });
            vector<float> out;
            string why;

            if (!render1(pluginPath, spec, "smp", "out", 1, 1, out, why))
            {
                good = false;
                detail = why;
            }
            else if (fabs(out[0] / TH_MAX - levels[chosen[k]]) > 0.0001f)
            {
                good = false;
                detail = "select " + num(selects[k]) + " gave " +
                         num(out[0] / TH_MAX);
            }
        }

        okOrFail(good, "osc::sample: select chooses the three layers at "
                       "the split boundaries", detail);

        vector<NodeSpec> spec = sampleGraph(layers[0], 440, 440, 0, 16);
        spec[0].texts.push_back(Text{ "file2", layers[1] });
        spec[0].values.push_back(Value{ "select", 1 });
        spec[0].values.push_back(Value{ "trigger", 1 });
        vector<float> out;
        string why;

        if (!render1(pluginPath, spec, "smp", "out", 1, 1, out, why))
            fail("osc::sample: missing layer fallback renders", why);
        else
            okOrFail(fabs(out[0] / TH_MAX - levels[1]) < 0.0001f,
                     "osc::sample: an empty file3 falls back to file2", "");

        spec[0].texts.clear();
        spec[0].texts.push_back(Text{ "file", layers[0] });

        if (!render1(pluginPath, spec, "smp", "out", 1, 1, out, why))
            fail("osc::sample: single-file fallback renders", why);
        else
            okOrFail(fabs(out[0] / TH_MAX - levels[0]) < 0.0001f,
                     "osc::sample: missing file2 and file3 use file", "");
    }

    /* The alternate counter belongs to the synth, not the voice: send
       successive edges to two nodes and verify 1, 2, 3, 1. In the same
       run, every edge is counted once, including after the other voice
       was last to sound. */
    {
        thSynth synth(pluginPath, 1, TH_DEFAULT_SAMPLES);
        thSynthTree tree("sample-layers", &synth);
        vector<NodeSpec> spec = sampleGraph(layers[0], 440, 440, 0, 16);
        spec[0].texts.push_back(Text{ "file2", layers[1] });
        spec[0].texts.push_back(Text{ "file3", layers[2] });
        spec[0].values.push_back(Value{ "alternate", 1 });
        NodeSpec other = spec[0];
        other.name = "other";
        spec.push_back(other);
        string why;

        if (!buildGraph(synth, tree, spec, why))
            fail("osc::sample: alternate graph loads", why);
        else
        {
            thNode *nodes[] = { tree.findNode("smp"), tree.findNode("other") };
            bool good = true;
            string detail;

            for (unsigned hit = 0; hit < 4 && good; hit++)
            {
                nodes[0]->setArg("trigger", 0.0f);
                nodes[1]->setArg("trigger", 0.0f);
                tree.setActiveNodes();
                tree.process(1);

                thNode *n = nodes[hit % 2];
                n->setArg("trigger", 1.0f);
                tree.setActiveNodes();
                tree.process(1);
                const float actual = (*n->getArg("out"))[0] / TH_MAX;

                if (fabs(actual - levels[hit % 3]) > 0.0001f)
                {
                    good = false;
                    detail = "hit " + num(hit) + " gave " + num(actual);
                }
            }

            okOrFail(good, "osc::sample: alternate cycles 1, 2, 3, 1 "
                           "across voices", detail);
        }
    }

    {
        thSynth synth(pluginPath, 1, TH_DEFAULT_SAMPLES);
        thSynthTree tree("sample-select-latch", &synth);
        vector<NodeSpec> spec = sampleGraph(layers[0], 440, 440, 0, 16);
        spec[0].texts.push_back(Text{ "file2", layers[1] });
        spec[0].texts.push_back(Text{ "file3", layers[2] });
        string why;

        if (!buildGraph(synth, tree, spec, why))
            fail("osc::sample: select latch graph loads", why);
        else
        {
            thNode *n = tree.findNode("smp");
            n->setArg("trigger", 1.0f);
            n->setArg("select", 0.0f);
            tree.setActiveNodes();
            tree.process(1);

            n->setArg("select", 1.0f);
            tree.setActiveNodes();
            tree.process(1);
            const float held = (*n->getArg("out"))[0] / TH_MAX;

            n->setArg("trigger", 0.0f);
            tree.setActiveNodes();
            tree.process(1);
            n->setArg("trigger", 1.0f);
            tree.setActiveNodes();
            tree.process(1);
            const float next = (*n->getArg("out"))[0] / TH_MAX;

            okOrFail(fabs(held - 17.0f / 64 * 0.25f) < 0.0001f &&
                     fabs(next - levels[2]) < 0.0001f,
                     "osc::sample: select is latched until the next "
                     "trigger edge",
                     "held " + num(held) + ", next " + num(next));
        }
    }

    /* ---- `xfade' takes the seam out of a loop ---- */

    /* The ramp looped over its last 200 frames jumps from 0.999 back to
       0.8 every time round, which is the click a loop cut from a
       recording makes. With a hundred frames of crossfade the largest
       step from one sample to the next has to be under a tenth of that
       jump. Not down to the ramp's own slope: the two ends of a ramp are
       correlated, and an equal-power fade of correlated signals bulges by
       up to a factor of root two in the middle, which on a jump of a
       fifth is a step of about 0.015 a frame -- and
       at `xfade = 0' the jump has to still be there, to the bit, so the
       knob at zero is the loop it always was. */
    {
        vector<NodeSpec> plain = sampleGraph("ramp.wav", 440, 440, 200, 0);
        vector<NodeSpec> faded = plain;
        vector<NodeSpec> zero = plain;
        vector<float> a, b, z;
        string why;

        faded[0].values.push_back(Value{ "xfade", 100 });
        zero[0].values.push_back(Value{ "xfade", 0 });

        if (!render1(pluginPath, plain, "smp", "out", 256, 3000, a, why) ||
            !render1(pluginPath, faded, "smp", "out", 256, 3000, b, why) ||
            !render1(pluginPath, zero, "smp", "out", 256, 3000, z, why))
            fail("osc::sample renders", why);
        else
        {
            double da = 0, db = 0;

            for (size_t i = 1; i < a.size(); i++)
            {
                da = fmax(da, fabs((double)a[i] - a[i - 1]) / TH_MAX);
                db = fmax(db, fabs((double)b[i] - b[i - 1]) / TH_MAX);
            }

            okOrFail(da > 0.15 && db < da / 10 && a == z,
                     "osc::sample: `xfade' crossfades the loop's seam, and "
                     "at 0 leaves the jump exactly where it was",
                     "largest step " + num(da) + " without, " + num(db) +
                     " with");
        }
    }

    windowsAgree(pluginPath, sampleGraph("ramp.wav", 331, 440, 200, 17),
                 "smp", "out",
                 "osc::sample: the same playback at one sample a window and "
                 "at five hundred");

    std::filesystem::remove_all(dir, ec);
}

/* ---- misc::drift -------------------------------------------------------- */

static vector<NodeSpec> driftGraph (float rate, float depth, float center,
                                    float seed)
{
    vector<NodeSpec> spec;
    NodeSpec d;

    d.name = "drift";
    d.spelling = "misc/drift";

    Value r = { "rate", rate };
    Value dp = { "depth", depth };
    Value c = { "center", center };
    Value s = { "seed", seed };

    d.values.push_back(r);
    d.values.push_back(dp);
    d.values.push_back(c);
    d.values.push_back(s);

    spec.push_back(d);

    return spec;
}

static void checkDrift (const string &pluginPath)
{
    const float rate = 4, depth = 0.3f, center = 0.5f;
    vector<float> a, b, c;
    string why;

    if (!render1(pluginPath, driftGraph(rate, depth, center, 7), "drift",
                 "out", 256, 10 * TH_DEFAULT_SAMPLES, a, why) ||
        !render1(pluginPath, driftGraph(rate, depth, center, 7), "drift",
                 "out", 256, 10 * TH_DEFAULT_SAMPLES, b, why) ||
        !render1(pluginPath, driftGraph(rate, depth, center, 8), "drift",
                 "out", 256, 10 * TH_DEFAULT_SAMPLES, c, why))
    {
        fail("misc::drift renders", why);
        return;
    }

    /* ---- inside center +- depth, and using it ---- */
    {
        double lo = 1e9, hi = -1e9;

        for (size_t i = 0; i < a.size(); i++)
        {
            lo = fmin(lo, a[i]);
            hi = fmax(hi, a[i]);
        }

        okOrFail(lo >= center - depth - 1e-6 && hi <= center + depth + 1e-6 &&
                 hi - lo > depth,
                 "misc::drift: stays within `depth' of `center' and wanders "
                 "across most of it",
                 "ran " + num(lo) + " to " + num(hi));
    }

    /* ---- smooth: no step, and no corner ---- */

    /* A half cosine from one target to the next moves at most
       pi * rate * depth / rate-of-samples a sample (the targets are at
       most 2 * depth apart), and its slope changes by at most
       pi^2 * rate^2 * depth / rate-of-samples^2 a sample -- at the joins
       as well as between them, which is what `continuous derivative'
       comes to on a tape. A latch-and-lag drift fails the second by
       orders of magnitude at every join.

       Forty hertz of full-scale drift about zero, and not the slow one
       above: at four hertz the change of slope is 2e-8 a sample, a third
       of one float step at 0.5, and what the check would read is the
       rounding. */
    {
        const double fast = 40, sr = TH_DEFAULT_SAMPLES;
        const double slope = M_PI * fast / sr;
        const double bend = M_PI * M_PI * fast * fast / (sr * sr);
        vector<float> f;
        double d1 = 0, d2 = 0;

        if (!render1(pluginPath, driftGraph((float)fast, 1, 0, 7), "drift",
                     "out", 256, 2 * TH_DEFAULT_SAMPLES, f, why))
        {
            fail("misc::drift renders", why);
            return;
        }

        for (size_t i = 2; i < f.size(); i++)
        {
            d1 = fmax(d1, fabs((double)f[i] - f[i - 1]));
            d2 = fmax(d2, fabs((double)f[i] - 2.0 * f[i - 1] + f[i - 2]));
        }

        okOrFail(d1 <= slope * 1.01 + 2.4e-7 && d2 <= bend * 1.1 + 4.8e-7,
                 "misc::drift: moves by a half cosine, with no step and no "
                 "corner, joins included",
                 "largest step " + num(d1) + " against " + num(slope) +
                 ", largest change of slope " + num(d2) + " against " +
                 num(bend));
    }

    okOrFail(a == b && a != c,
             "misc::drift: the same seed wanders the same way, and another "
             "seed does not", "");

    {
        vector<float> held;

        if (!render1(pluginPath, driftGraph(0, depth, center, 7), "drift",
                     "out", 256, TH_DEFAULT_SAMPLES, held, why))
            fail("misc::drift renders", why);
        else
        {
            bool still = true;

            for (size_t i = 1; i < held.size() && still; i++)
                if (held[i] != held[0])
                    still = false;

            okOrFail(still, "misc::drift: at `rate = 0' it holds", "");
        }
    }

    windowsAgree(pluginPath, driftGraph(40, depth, center, 7), "drift", "out",
                 "misc::drift: the same path at one sample a window and at "
                 "five hundred");
}

/* ---- osc::grain --------------------------------------------------------- */

struct GrainArgs
{
    float position, spread, size, density, pitch, jitter, seed;
};

static vector<NodeSpec> grainGraph (const char *file, const GrainArgs &g)
{
    vector<NodeSpec> spec;
    NodeSpec n;

    n.name = "grain";
    n.spelling = "osc/grain";

    Text f = { "file", file };

    n.texts.push_back(f);
    n.values.push_back(Value{ "position", g.position });
    n.values.push_back(Value{ "spread", g.spread });
    n.values.push_back(Value{ "size", g.size });
    n.values.push_back(Value{ "density", g.density });
    n.values.push_back(Value{ "pitch", g.pitch });
    n.values.push_back(Value{ "jitter", g.jitter });
    n.values.push_back(Value{ "seed", g.seed });

    spec.push_back(n);

    return spec;
}

/* The power within `half' hertz of `hz', from bins a hertz apart over a
   second from `from'. */
static double powerNear (const vector<float> &v, size_t from, double hz,
                         double half)
{
    double sum = 0;

    for (double f = hz - half; f <= hz + half; f += 1)
    {
        const double a = bin(v, from, TH_DEFAULT_SAMPLES, f);

        sum += a * a;
    }

    return sum;
}

static void checkGrain (const string &pluginPath)
{
    const string dir = thUtil::tempFile("statecheck-grains-");
    std::error_code ec;

    if (dir.empty())
    {
        fail("osc::grain: could not make a scratch directory", "");
        return;
    }

    std::filesystem::remove(dir);
    std::filesystem::create_directories(dir + "/samples", ec);

    /* Two seconds of a 220 Hz sine at half scale: a source whose every
       grain is the same pitch, so what comes out says what the grains did
       to it and nothing else. */
    vector<float> sine(2 * TH_DEFAULT_SAMPLES);

    for (size_t i = 0; i < sine.size(); i++)
        sine[i] = 0.5f * (float)sin(2 * M_PI * 220 * (double)i /
                                    TH_DEFAULT_SAMPLES);

    if (ec || !writeWav(dir + "/samples/sine220.wav", sine,
                        TH_DEFAULT_SAMPLES))
    {
        fail("osc::grain: could not write the scratch wav", ec.message());
        std::filesystem::remove_all(dir, ec);
        return;
    }

#ifdef _WIN32
    _putenv_s("THINK_DSP_PATH", dir.c_str());
#else
    setenv("THINK_DSP_PATH", dir.c_str(), 1);
#endif

    const float ms = TH_DEFAULT_SAMPLES / 1000.0f;

    /* ---- dense enough and there is no grain to hear ---- */

    /* A thousand grains a second, four milliseconds each, all from one
       place: the RMS of every twentieth of a second, over a second, has
       to be within five percent of the mean -- which is a cloud with no
       gaps in it and no pulse at the launch rate either. */
    {
        vector<float> got;
        string why;
        const GrainArgs g = { 0.5f, 0, 4 * ms, 1000, 1, 0, 1 };

        if (!render1(pluginPath, grainGraph("sine220.wav", g), "grain", "out",
                     256, TH_DEFAULT_SAMPLES + TH_DEFAULT_SAMPLES / 10, got,
                     why))
            fail("osc::grain renders", why);
        else
        {
            const size_t block = TH_DEFAULT_SAMPLES / 20;
            vector<double> level;
            double mean = 0, worst = 0;

            for (size_t at = TH_DEFAULT_SAMPLES / 10;
                 at + block <= got.size(); at += block)
            {
                level.push_back(rms(vector<float>(got.begin() + at,
                                                  got.begin() + at + block),
                                    0));
                mean += level.back();
            }

            mean /= level.size();

            for (size_t k = 0; k < level.size(); k++)
                worst = fmax(worst, fabs(level[k] / mean - 1));

            okOrFail(mean > 0.05 && worst < 0.05,
                     "osc::grain: a thousand four-millisecond grains a second "
                     "are a steady sound",
                     "mean RMS " + num(mean) + ", worst block " +
                     num(worst * 100) + "% off it");
        }
    }

    /* ---- pitch is pitch ---- */

    /* Grains from all over the file at twice its speed: the power within
       ten hertz of 440 has to be ten times what is left within ten of
       220, and three times what lands a semitone above 440. */
    {
        vector<float> got;
        string why;
        const GrainArgs g = { 0.5f, 0.3f, 50 * ms, 100, 2, 0, 1 };

        if (!render1(pluginPath, grainGraph("sine220.wav", g), "grain", "out",
                     256, 2 * TH_DEFAULT_SAMPLES, got, why))
            fail("osc::grain renders", why);
        else
        {
            const size_t from = TH_DEFAULT_SAMPLES / 2;
            const double up = powerNear(got, from, 440, 10);
            const double left = powerNear(got, from, 220, 10);
            const double off = powerNear(got, from, 466, 10);

            okOrFail(up > 10 * left && up > 3 * off,
                     "osc::grain: `pitch = 2' plays a 220 Hz file at 440",
                     "near 440 " + num(up) + ", near 220 " + num(left) +
                     ", near 466 " + num(off));
        }
    }

    /* ---- and a keyboard plays it ---- */
    {
        vector<NodeSpec> spec =
            grainGraph("sine220.wav", GrainArgs{ 0.5f, 0.3f, 50 * ms, 100,
                                                 1, 0, 1 });
        vector<float> got;
        string why;

        spec[0].values.push_back(Value{ "freq", 330 });
        spec[0].values.push_back(Value{ "root", 220 });

        if (!render1(pluginPath, spec, "grain", "out", 256,
                     2 * TH_DEFAULT_SAMPLES, got, why))
            fail("osc::grain renders", why);
        else
        {
            const size_t from = TH_DEFAULT_SAMPLES / 2;
            const double fifth = powerNear(got, from, 330, 10);
            const double left = powerNear(got, from, 220, 10);

            okOrFail(fifth > 10 * left,
                     "osc::grain: `freq = 330, root = 220' plays the file a "
                     "fifth up",
                     "near 330 " + num(fifth) + ", near 220 " + num(left));
        }
    }

    /* ---- the same cloud twice ---- */
    {
        const GrainArgs g = { 0.3f, 0.5f, 30 * ms, 300, 1, 3, 5 };
        GrainArgs other = g;
        vector<float> a, b, c;
        string why;

        other.seed = 6;

        if (!render1(pluginPath, grainGraph("sine220.wav", g), "grain",
                     "out", 256, TH_DEFAULT_SAMPLES, a, why) ||
            !render1(pluginPath, grainGraph("sine220.wav", g), "grain",
                     "out", 256, TH_DEFAULT_SAMPLES, b, why) ||
            !render1(pluginPath, grainGraph("sine220.wav", other), "grain",
                     "out", 256, TH_DEFAULT_SAMPLES, c, why))
            fail("osc::grain renders", why);
        else
            okOrFail(a == b && a != c,
                     "osc::grain: two fresh voices with one seed are the same "
                     "cloud, bit for bit, and another seed is another", "");
    }

    /* ---- a position swept smoothly does not click ---- */

    /* The same cloud with `position' held and with it swept most of the
       way through the file and back by a sine at half a hertz. The source
       is one steady sine, so a grain that jumped -- which a node that
       re-read `position' inside a sounding grain would do -- is a step,
       and a step is a second difference the size of the step, where a
       220 Hz sine's is a thousandth of its amplitude. Relative to each
       render's own peak, since moving the reads moves how the grains'
       phases line up and so the level, which is not a click. */
    {
        vector<NodeSpec> held = grainGraph("sine220.wav",
            GrainArgs{ 0.5f, 0, 40 * ms, 200, 1, 0, 1 });
        vector<NodeSpec> swept = held;
        NodeSpec lfo, place;
        vector<float> a, b;
        string why;

        lfo.name = "lfo";
        lfo.spelling = "osc/simple";
        lfo.values.push_back(Value{ "freq", 0.5f });
        lfo.values.push_back(Value{ "amp", 0.4f });
        lfo.values.push_back(Value{ "waveform", 0 });

        place.name = "place";
        place.spelling = "math/add";
        place.values.push_back(Value{ "in1", 0.5f });
        place.wires.push_back(Wire{ "in0", "lfo", "out" });

        swept[0].values.erase(swept[0].values.begin());
        swept[0].wires.push_back(Wire{ "position", "place", "out" });
        swept.insert(swept.begin(), place);
        swept.insert(swept.begin(), lfo);

        if (!render1(pluginPath, held, "grain", "out", 256,
                     2 * TH_DEFAULT_SAMPLES, a, why) ||
            !render1(pluginPath, swept, "grain", "out", 256,
                     2 * TH_DEFAULT_SAMPLES, b, why))
            fail("osc::grain renders", why);
        else
        {
            double da = 0, db = 0;

            for (size_t i = TH_DEFAULT_SAMPLES / 10; i < a.size(); i++)
            {
                da = fmax(da, fabs((double)a[i] - 2.0 * a[i - 1] + a[i - 2]));
                db = fmax(db, fabs((double)b[i] - 2.0 * b[i - 1] + b[i - 2]));
            }

            da /= peak(a, TH_DEFAULT_SAMPLES / 10);
            db /= peak(b, TH_DEFAULT_SAMPLES / 10);

            okOrFail(db < da * 1.5 && db < 0.01,
                     "osc::grain: a position swept through the file does not "
                     "click",
                     "largest second difference over the peak " + num(db) +
                     " swept, " + num(da) + " held");
        }
    }

    /* ---- the live ring ---- */

    /* `source = 1' on a 440 Hz sine it is fed: the grains read what the
       ring has recorded, at `pitch' times its speed, so the output is at
       440 and at `pitch = 2' at 880. With `freeze' held from the start the
       ring never records and the cloud is silent. */
    {
        auto live = [&](float pitch, float freeze, vector<float> &got,
                        string &why) {
            vector<NodeSpec> spec;
            NodeSpec src, g;

            src.name = "src";
            src.spelling = "osc/simple";
            src.values.push_back(Value{ "freq", 440 });
            src.values.push_back(Value{ "amp", 0.5f });
            src.values.push_back(Value{ "waveform", 0 });

            g.name = "grain";
            g.spelling = "osc/grain";
            g.values.push_back(Value{ "source", 1 });
            g.values.push_back(Value{ "position", 0.05f });
            g.values.push_back(Value{ "spread", 0.02f });
            g.values.push_back(Value{ "size", 50 * ms });
            g.values.push_back(Value{ "density", 100 });
            g.values.push_back(Value{ "pitch", pitch });
            g.values.push_back(Value{ "freeze", freeze });
            g.wires.push_back(Wire{ "in", "src", "out" });

            spec.push_back(src);
            spec.push_back(g);

            return render1(pluginPath, spec, "grain", "out", 256,
                           2 * TH_DEFAULT_SAMPLES, got, why);
        };

        vector<float> one, two, frozen;
        string why;

        if (!live(1, 0, one, why) || !live(2, 0, two, why) ||
            !live(1, 1, frozen, why))
            fail("osc::grain renders", why);
        else
        {
            const size_t from = TH_DEFAULT_SAMPLES;
            const double at440 = powerNear(one, from, 440, 10);
            const double at880 = powerNear(two, from, 880, 10);
            const double left = powerNear(two, from, 440, 10);

            okOrFail(at440 > 0.01 && at880 > 10 * left &&
                     peak(frozen, 0) == 0,
                     "osc::grain: a live ring plays what it heard, shifted by "
                     "`pitch', and a ring frozen from the start is silent",
                     "440 at pitch 1: " + num(at440) + "; at pitch 2, 880: " +
                     num(at880) + " and 440: " + num(left) +
                     "; frozen peak " + num(peak(frozen, 0)));
        }
    }

    /* ---- finite at every corner ---- */
    {
        static const float sizes[] = { -1, 0, 1, 44100, 1e9f };
        static const float densities[] = { -5, 0, 2000, 1e9f };
        static const float pitches[] = { -1, 0, 0.01f, 100 };
        static const float places[] = { -2, 0, 1, 7 };
        bool finite = true;
        string detail;

        for (float sz : sizes)
            for (float dn : densities)
                for (float pt : pitches)
                    for (float pl : places)
                    {
                        vector<float> got;
                        string why;
                        const GrainArgs g = { pl, pl, sz, dn, pt, 100, 1 };

                        if (!render1(pluginPath,
                                     grainGraph("sine220.wav", g), "grain",
                                     "out", 256, 4410, got, why))
                        {
                            fail("osc::grain renders", why);
                            std::filesystem::remove_all(dir, ec);
                            return;
                        }

                        if (finite && !(allFinite(got) && peak(got, 0) < 4))
                        {
                            finite = false;
                            detail = "size " + num(sz) + ", density " +
                                     num(dn) + ", pitch " + num(pt) +
                                     ", position " + num(pl) + ": peak " +
                                     num(peak(got, 0));
                        }
                    }

        okOrFail(finite, "osc::grain: finite and bounded at every corner",
                 detail);
    }

    {
        vector<float> got;
        string why;
        const GrainArgs g = { 0.5f, 0.1f, 10 * ms, 100, 1, 0, 1 };

        if (!render1(pluginPath, grainGraph("nosuchthing.wav", g), "grain",
                     "out", 256, 4410, got, why))
            fail("osc::grain renders", why);
        else
            okOrFail(allFinite(got) && peak(got, 0) == 0,
                     "osc::grain: a file that is not there is silence", "");
    }

    windowsAgree(pluginPath,
                 grainGraph("sine220.wav",
                            GrainArgs{ 0.2f, 0.3f, 7 * ms, 700, 1.3f, 2, 4 }),
                 "grain", "out",
                 "osc::grain: the same cloud at one sample a window and at "
                 "five hundred");

    std::filesystem::remove_all(dir, ec);
}

/* ---- osc::pad ----------------------------------------------------------- */

static vector<NodeSpec> padGraph (float freq, float partials, float bandwidth,
                                  float bwscale, float tilt, float stretch)
{
    vector<NodeSpec> spec;
    NodeSpec p;

    p.name = "pad";
    p.spelling = "osc/pad";
    p.values.push_back(Value{ "freq", freq });
    p.values.push_back(Value{ "partials", partials });
    p.values.push_back(Value{ "bandwidth", bandwidth });
    p.values.push_back(Value{ "bwscale", bwscale });
    p.values.push_back(Value{ "tilt", tilt });
    p.values.push_back(Value{ "stretch", stretch });

    spec.push_back(p);

    return spec;
}

/* The power-weighted mean and standard deviation of the spectrum within
   `half' hertz of `hz', and the total power there. */
static void bandStats (const vector<double> &power, size_t n, double hz,
                       double half, double &mean, double &sd, double &total)
{
    const double perBin = (double)TH_DEFAULT_SAMPLES / (double)n;
    double s0 = 0, s1 = 0, s2 = 0;

    for (size_t k = (size_t)fmax(1, (hz - half) / perBin);
         k < power.size() && k * perBin <= hz + half; k++)
    {
        const double f = k * perBin;

        s0 += power[k];
        s1 += power[k] * f;
        s2 += power[k] * f * f;
    }

    total = s0;
    mean = s0 > 0 ? s1 / s0 : 0;
    sd = s0 > 0 ? sqrt(fmax(0, s2 / s0 - mean * mean)) : 0;
}

static void checkPad (const string &pluginPath)
{
    const size_t n = 131072;              /* three seconds, 0.34 Hz a bin */
    const float c4 = 261.63f;

    /* ---- at no bandwidth, lines ---- */

    /* Sixteen partials at middle C, which is octave 0's own base, so the
       table is read at one sample a sample: nearly all the power has to be
       within a hertz and a half of a harmonic -- the Hann window's own
       main lobe, and nothing wider. */
    {
        vector<float> got;
        string why;

        if (!render1(pluginPath, padGraph(c4, 16, 0, 1, -6, 0), "pad", "out",
                     256, (unsigned)n + 4410, got, why))
        {
            fail("osc::pad renders", why);
            return;
        }

        const vector<double> power = powerSpectrum(got, 4410, n);
        const double perBin = (double)TH_DEFAULT_SAMPLES / (double)n;
        double all = 0, lines = 0;

        for (size_t k = 1; k < power.size(); k++)
        {
            const double f = k * perBin;
            const double h = floor(f / c4 + 0.5);

            all += power[k];

            if (h >= 1 && h <= 16 && fabs(f - h * c4) <= 1.5)
                lines += power[k];
        }

        okOrFail(all > 0 && lines / all > 0.99,
                 "osc::pad: at `bandwidth = 0' the table is a sum of sines "
                 "at the harmonics",
                 num(100 * lines / all) + "% of the power on them");
    }

    /* ---- at 40 cents, bands of 40 cents ---- */

    /* A partial's band is a Gaussian in magnitude whose full width is
       `bandwidth' cents of its own pitch at `bwscale = 1' -- a standard
       deviation of half that in hertz, 3.1 Hz at the fundamental and 12.2
       at the fourth -- and so a Gaussian in power narrower by root two.
       Measured as the power-weighted spread within four sigmas either
       side, it has to come out within a quarter of that, centered on the
       partial. The phases are random, so each bin's power is too; a
       quarter is what that leaves room for. */
    {
        vector<float> got;
        string why;

        if (!render1(pluginPath, padGraph(c4, 8, 40, 1, 0, 0), "pad", "out",
                     256, (unsigned)n + 4410, got, why))
        {
            fail("osc::pad renders", why);
            return;
        }

        const vector<double> power = powerSpectrum(got, 4410, n);
        const double width = (pow(2.0, 40.0 / 1200) - 1) * c4;
        bool good = true;
        string detail;

        for (int h = 1; h <= 4; h *= 4)
        {
            const double sigma = width * h / 2;
            double mean, sd, total;

            bandStats(power, n, c4 * h, 4 * sigma, mean, sd, total);

            if (!(fabs(mean - c4 * h) < sigma / 4 &&
                  fabs(sd / (sigma / sqrt(2.0)) - 1) < 0.25))
            {
                good = false;
                detail = "partial " + num(h) + ": centered on " + num(mean) +
                         " Hz with a spread of " + num(sd) + " against " +
                         num(sigma);
            }
        }

        okOrFail(good, "osc::pad: at 40 cents each partial's power is a band "
                       "that wide, centered on the partial", detail);
    }

    /* ---- the same table twice, and a second side that is not the first */
    {
        vector<Watch> watch;
        vector< vector<float> > a, b;
        string why;

        watch.push_back(Watch{ "pad", "out" });
        watch.push_back(Watch{ "pad", "out2" });

        if (!render(pluginPath, padGraph(220, 32, 25, 1, -6, 0.0004f), watch,
                    256, TH_DEFAULT_SAMPLES, a, why) ||
            !render(pluginPath, padGraph(220, 32, 25, 1, -6, 0.0004f), watch,
                    256, TH_DEFAULT_SAMPLES, b, why))
            fail("osc::pad renders", why);
        else
        {
            double ll = 0, rr = 0, lr = 0;

            for (size_t i = 0; i < a[0].size(); i++)
            {
                ll += (double)a[0][i] * a[0][i];
                rr += (double)a[1][i] * a[1][i];
                lr += (double)a[0][i] * a[1][i];
            }

            okOrFail(a == b, "osc::pad: two synths render the same samples",
                     "");
            okOrFail(fabs(lr / sqrt(ll * rr)) < 0.2,
                     "osc::pad: `out2' is the same pad, decorrelated",
                     "correlation " + num(lr / sqrt(ll * rr)));
        }
    }

    /* ---- finite and in range at every corner ---- */
    {
        /* Every table is an FFT of 2^18, so the ends only: the middle of
           each range is what the checks above already render. */
        static const float freqs[] = { -1, 20, 20000 };
        static const float counts[] = { 0, 1e6f };
        static const float widths[] = { -5, 1e9f };
        static const float tilts[] = { -100, 100 };
        bool finite = true;
        string detail;

        for (float fr : freqs)
            for (float pc : counts)
                for (float bw : widths)
                    for (float tl : tilts)
                    {
                        vector<float> got;
                        string why;

                        if (!render1(pluginPath,
                                     padGraph(fr, pc, bw, 2, tl, 1), "pad",
                                     "out", 256, 2000, got, why))
                        {
                            fail("osc::pad renders", why);
                            return;
                        }

                        if (finite && !(allFinite(got) &&
                                        peak(got, 0) <= TH_MAX))
                        {
                            finite = false;
                            detail = "freq " + num(fr) + ", partials " +
                                     num(pc) + ", bandwidth " + num(bw) +
                                     ", tilt " + num(tl);
                        }
                    }

        okOrFail(finite, "osc::pad: finite and in range at every corner",
                 detail);
    }

    windowsAgree(pluginPath, padGraph(331, 24, 30, 1, -6, 0), "pad", "out",
                 "osc::pad: the same pad at one sample a window and at five "
                 "hundred");
}

/* ---- filt::pianostring -------------------------------------------------- */

/* What the node claims, and what each check is for:
 *
 *   - the fundamental is `freq' to within half a cent, from A0 to C7, with
 *     the dispersion filter's delay and the loss filter's in the loop, and
 *     within a cent up to the top of the MIDI range;
 *
 *   - the partials sit at n f0 sqrt(1 + b n^2), f0 sqrt(1 + b) = `freq', to
 *     within a few cents, for every partial the fit covers -- the stretch
 *     that is the reason the node exists, and which filt::comb cannot make;
 *
 *   - the fundamental falls sixty decibels in `decay', and with `hidecay'
 *     shorter the partials near 3 kHz fall in that instead;
 *
 *   - `gate = 0' brings the string down in `damper', stiff or not, and
 *     `play' follows the string rather than the gate, and ends even when
 *     the excitation has a net DC;
 *
 *   - and a window boundary is not an event.
 */

static vector<NodeSpec> pianoGraph (float freq, float b, float decay,
                                    float hidecay, float damper, float gate,
                                    float pulse = 1)
{
    vector<NodeSpec> spec;
    NodeSpec src, str;

    src.name = "src";
    src.spelling = "env/ad";

    Value a = { "a", 0 };
    Value d = { "d", pulse };
    Value p = { "p", TH_MAX };

    src.values.push_back(a);
    src.values.push_back(d);
    src.values.push_back(p);

    str.name = "string";
    str.spelling = "filt/pianostring";

    Value f = { "freq", freq };
    Value bv = { "b", b };
    Value dc = { "decay", decay };
    Value hd = { "hidecay", hidecay };
    Value dm = { "damper", damper };
    Value gt = { "gate", gate };
    Wire  in = { "in", "src", "out" };

    str.values.push_back(f);
    str.values.push_back(bv);
    str.values.push_back(dc);
    str.values.push_back(hd);
    str.values.push_back(dm);
    str.values.push_back(gt);
    str.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(str);

    return spec;
}

/* The magnitude of `n' samples from `from' at `hz', through a four-term
   Blackman-Harris window: its sidelobes are 92 dB down, so a partial is
   not pulled by its neighbors, and its main lobe is four bins either
   side, which a one-second window keeps inside the 27 Hz between A0's
   partials. */
static double windowedMag (const vector<float> &v, size_t from, size_t n,
                           double hz)
{
    const double w = 2.0 * M_PI * hz / TH_DEFAULT_SAMPLES;
    double re = 0, im = 0;

    for (size_t i = 0; i < n && from + i < v.size(); i++)
    {
        const double x = 2.0 * M_PI * (double)i / (double)(n - 1);
        const double win = 0.35875 - 0.48829 * cos(x) +
                           0.14128 * cos(2 * x) - 0.01168 * cos(3 * x);

        re += win * v[from + i] * cos(w * i);
        im -= win * v[from + i] * sin(w * i);
    }

    return sqrt(re * re + im * im);
}

/* Where the spectrum peaks within `span' cents of `hz': a scan at a cent
   and a golden-section search on the best of it. */
static double peakNear (const vector<float> &v, size_t from, size_t n,
                        double hz, double span)
{
    const double golden = 0.6180339887498949;
    double best = 0, bestAt = hz, lo, hi;

    for (double c = -span; c <= span; c += 1)
    {
        const double f = hz * pow(2.0, c / 1200);
        const double m = windowedMag(v, from, n, f);

        if (m > best)
        {
            best = m;
            bestAt = f;
        }
    }

    lo = bestAt * pow(2.0, -1.0 / 1200);
    hi = bestAt * pow(2.0, 1.0 / 1200);

    for (int i = 0; i < 30; i++)
    {
        const double x0 = hi - golden * (hi - lo);
        const double x1 = lo + golden * (hi - lo);

        if (windowedMag(v, from, n, x0) > windowedMag(v, from, n, x1))
            hi = x1;
        else
            lo = x0;
    }

    return 0.5 * (lo + hi);
}

/* A plausible piano's inharmonicity by MIDI key: about 2e-4 at A0, least
   in the tenor, 1.6e-2 at C8. */
static double pianoB (int key)
{
    return 1e-4 * (pow(2.0, (key - 48) * 0.1218) +
                   pow(2.0, (48 - key) * 0.0385));
}

static void checkPianostring (const string &pluginPath)
{
    const double rate = TH_DEFAULT_SAMPLES;

    /* ---- the fundamental and the partials ---- */

    /* An impulse into a string that barely decays, and a second of it
       after the first twentieth. Every partial under 6 kHz up to the
       eighth, against the stiff-string formula with its first partial at
       `freq': the fundamental to half a cent, the rest to two. */
    {
        static const int keys[] = { 21, 33, 45, 60, 69, 81, 96 };

        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
        {
            const double hz = 440 * pow(2.0, (keys[k] - 69) / 12.0);
            const double b = pianoB(keys[k]);
            const double f0 = hz / sqrt(1 + b);
            const size_t from = (size_t)(rate / 20);
            const size_t n = (size_t)rate;
            vector<float> out;
            string why, detail;
            double worst = 0;
            bool good = true;

            if (!render1(pluginPath,
                         pianoGraph((float)hz, (float)b, 60, 0, 0, 1),
                         "string", "out", 256, (unsigned)(from + n), out,
                         why))
            {
                fail("filt::pianostring renders", why);
                return;
            }

            for (int p = 1; p <= 8; p++)
            {
                const double want = p == 1 ? hz
                                           : p * f0 * sqrt(1 + b * p * p);
                double got, off;

                if (want > 6000)
                    break;

                got = peakNear(out, from, n, want, 50);
                off = cents(got, want);

                if (fabs(off) > fabs(worst))
                    worst = off;

                if (fabs(off) > (p == 1 ? 0.5 : 2))
                {
                    good = false;
                    detail = "key " + num(keys[k]) + " partial " + num(p) +
                             " at " + num(got) + " Hz for " + num(want) +
                             ", " + num(off) + " cents";
                    break;
                }
            }

            okOrFail(good, "filt::pianostring: key " + num(keys[k]) +
                           "'s partials are n f0 sqrt(1 + b n^2) (worst " +
                           num(worst) + " cents)",
                     detail);
        }
    }

    /* The top of the MIDI range, where the fundamental is over a quarter of
       the rate and the Thiran's phase delay there is far from its
       fraction. */
    {
        static const int keys[] = { 108, 116, 120, 124, 127 };
        string detail;
        double worst = 0;
        bool good = true;

        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
        {
            const double hz = 440 * pow(2.0, (keys[k] - 69) / 12.0);
            const double b = pianoB(keys[k]) < 0.05 ? pianoB(keys[k]) : 0.05;
            const size_t from = (size_t)(rate / 20);
            const size_t n = (size_t)rate;
            vector<float> out;
            string why;
            double off;

            if (!render1(pluginPath,
                         pianoGraph((float)hz, (float)b, 60, 0, 0, 1),
                         "string", "out", 256, (unsigned)(from + n), out,
                         why))
            {
                fail("filt::pianostring renders", why);
                return;
            }

            off = cents(peakNear(out, from, n, hz, 60), hz);

            if (fabs(off) > fabs(worst))
                worst = off;

            if (fabs(off) > 1 && good)
            {
                good = false;
                detail = "key " + num(keys[k]) + " is " + num(off) +
                         " cents out";
            }
        }

        okOrFail(good, "filt::pianostring: keys 108 to 127 are `freq' to "
                       "a cent (worst " + num(worst) + " cents)",
                 detail);
    }

    /* ---- the two decays ---- */

    /* 500 Hz with no stiffness, so the sixth partial is at 3 kHz exactly,
       where `hidecay' is measured. Each partial's level a quarter-second
       in and three quarters in: half a second, so a T60 of t falls 30 / t
       decibels between them. */
    {
        static const float hidecays[] = { 0, 0.5f };

        for (size_t h = 0; h < 2; h++)
        {
            const double f0 = 500;
            const float decay = 2;
            const size_t n = (size_t)(rate / 10);
            const size_t t1 = (size_t)(rate / 4), t2 = (size_t)(rate * 3 / 4);
            vector<float> out;
            string why;

            if (!render1(pluginPath,
                         pianoGraph((float)f0, 0, decay, hidecays[h], 0, 1),
                         "string", "out", 256, (unsigned)(t2 + n), out, why))
            {
                fail("filt::pianostring renders", why);
                return;
            }

            for (int p = 1; p <= 6; p += 5)
            {
                const double hz = p * f0;
                const double fell =
                    20 * log10(windowedMag(out, t2, n, hz) /
                               windowedMag(out, t1, n, hz));
                const double t60 = -60 * (double)(t2 - t1) / rate / fell;
                const double want = (p == 6 && hidecays[h] > 0)
                                    ? hidecays[h] : decay;

                okOrFail(fabs(t60 / want - 1) < 0.1,
                         "filt::pianostring: " +
                         string(hidecays[h] > 0 ? "with `hidecay' 0.5, "
                                                : "with `hidecay' 0, ") +
                         "partial " + num(p) + " falls 60 dB in " +
                         num(want) + " s",
                         "it took " + num(t60) + " s");
            }
        }
    }

    /* ---- the damper, and `play' ---- */

    /* `gate = 0' from the start: a free string's `decay' and the damper's
       together are 1 / (1 / decay + 1 / damper). A harmonic string, and
       two stiff ones whose loops are longer than a period at the
       fundamental by what the dispersion filter delays it: A0 on the
       second-order cascade, F6 on the first-order one. */
    {
        /* The key is 0 for no stiffness. Seconds, and each window long
           enough to hold several periods. */
        struct Damped {
            double hz;
            int key;
            double decay, damper, t1, t2, n;
        };
        static const Damped cases[] = {
            { 220, 0, 10, 0.3, 0.05, 0.2, 0.05 },
            { 27.5, 21, 60, 1, 0.1, 0.6, 0.25 },
            { 1396.9, 89, 60, 0.3, 0.05, 0.2, 0.05 },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            const Damped &k = cases[c];
            const double b = k.key > 0 ? pianoB(k.key) : 0;
            const size_t n = (size_t)(rate * k.n);
            const size_t t1 = (size_t)(rate * k.t1);
            const size_t t2 = (size_t)(rate * k.t2);
            vector<float> out;
            string why;

            if (!render1(pluginPath,
                         pianoGraph((float)k.hz, (float)b, (float)k.decay, 0,
                                    (float)k.damper, 0),
                         "string", "out", 256, (unsigned)(t2 + n), out, why))
            {
                fail("filt::pianostring renders", why);
                return;
            }

            const double fell = 20 * log10(windowedMag(out, t2, n, k.hz) /
                                           windowedMag(out, t1, n, k.hz));
            const double t60 = -60 * (double)(t2 - t1) / rate / fell;
            const double want = 1 / (1 / k.decay + 1 / k.damper);

            okOrFail(fabs(t60 / want - 1) < 0.1,
                     "filt::pianostring: at " + num(k.hz) + " Hz, b " +
                     num(b) + ", with the damper down the string falls 60 "
                     "dB in `damper' and `decay' together",
                     "it took " + num(t60) + " s for " + num(want));
        }
    }

    {
        vector< vector<float> > got[2];
        static const float gates[] = { 1, 0 };
        bool bad = false;

        for (size_t g = 0; g < 2 && !bad; g++)
        {
            vector<Watch> watch;
            string why;
            Watch w = { "string", "play" };

            watch.push_back(w);

            if (!render(pluginPath, pianoGraph(220, 0, 10, 0, 0.1f, gates[g]),
                        watch, 256, (unsigned)rate, got[g], why))
            {
                fail("filt::pianostring renders", why);
                bad = true;
            }
        }

        if (!bad)
        {
            const vector<float> &held = got[0][0], &damped = got[1][0];

            okOrFail(held.back() == 1 && damped[0] == 1 && damped.back() == 0,
                     "filt::pianostring: `play' is 1 while the string "
                     "sounds and 0 once the damper has stopped it",
                     "held ends " + num(held.back()) + ", damped starts " +
                     num(damped[0]) + " and ends " + num(damped.back()));
        }
    }

    /* A pulse that is all one sign has a net DC, and the loop's gain at DC
       is its highest: with `hidecay' this much shorter than `decay' at
       1.5 kHz the loss filter's gain there is at its clamp. Unless the
       string keeps DC out, that mode outlasts the fundamental's two
       seconds by hours and `play' never falls. */
    {
        vector<float> play;
        string why;

        if (!render1(pluginPath,
                     pianoGraph(1500, 0, 2, 0.3f, 0, 1, (float)(rate / 100)),
                     "string", "play", 256, (unsigned)(rate * 4), play, why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        okOrFail(play.back() == 0,
                 "filt::pianostring: a pulse with a net DC ends in the "
                 "fundamental's time, not the DC's",
                 "`play' is still " + num(play.back()) + " after 4 s");
    }

    /* The line, the Thiran, sixteen sections' states, the loss filter and
       the damper's fade all cross a window boundary. A0, so the
       second-order cascade is the one in the loop. */
    windowsAgree(pluginPath,
                 pianoGraph(27.5f, (float)pianoB(21), 20, 2, 0.2f, 1),
                 "string", "out",
                 "filt::pianostring: the same string at one sample a window "
                 "and at five hundred");
    windowsAgree(pluginPath,
                 pianoGraph(440, (float)pianoB(69), 8, 1, 0.2f, 0),
                 "string", "out",
                 "filt::pianostring: and the first-order cascade, damped");
}

/* The same string with `strings', `unison' and `prompt' set. */
static vector<NodeSpec> unisonGraph (float freq, float b, float decay,
                                     float strings, float unison,
                                     float prompt)
{
    vector<NodeSpec> spec = pianoGraph(freq, b, decay, 0, 0, 1);
    Value n = { "strings", strings };
    Value u = { "unison", unison };
    Value p = { "prompt", prompt };

    spec[1].values.push_back(n);
    spec[1].values.push_back(u);
    spec[1].values.push_back(p);

    return spec;
}

/* The fundamental's level, in decibels, over `n' samples from `from'. */
static double levelAt (const vector<float> &v, double hz, double from,
                       double n)
{
    return 20 * log10(windowedMag(v, (size_t)(from * TH_DEFAULT_SAMPLES),
                                  (size_t)(n * TH_DEFAULT_SAMPLES), hz) +
                      1e-30);
}

static void checkPianoUnison (const string &pluginPath)
{
    const double rate = TH_DEFAULT_SAMPLES;

    /* ---- each string at its own pitch ---- */

    /* Uncoupled and fifty cents apart, so a one-second window resolves
       them: 440 Hz's neighbors are 13 Hz away. Two strings sit either side
       of the note and three on it and either side. */
    for (int strings = 2; strings <= 3; strings++)
    {
        const double f0 = 440;
        const size_t from = (size_t)(rate / 20), n = (size_t)rate;
        vector<float> out;
        string why, detail;
        bool good = true;

        if (!render1(pluginPath, unisonGraph((float)f0, 0, 60, strings, 50, 0),
                     "string", "out", 256, (unsigned)(from + n), out, why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        for (int k = 0; k < strings && good; k++)
        {
            const double want = f0 * pow(2.0, (k - 0.5 * (strings - 1)) *
                                              50 / 1200);
            const double got = peakNear(out, from, n, want, 15);

            if (fabs(cents(got, want)) > 0.5)
            {
                good = false;
                detail = "string " + num(k) + " at " + num(got) + " Hz for " +
                         num(want);
            }
        }

        okOrFail(good, "filt::pianostring: " + num(strings) + " strings "
                       "`unison' cents apart each ring at their own pitch",
                 detail);
    }

    /* ---- the bridge takes the in-phase motion in `prompt' ---- */

    /* In tune and struck together, the strings only ever move together,
       so they lose to the bridge and to their own loss at once:
       1 / (1/10 + 1/0.5) is 0.476 s. */
    {
        const double f0 = 262;
        vector<float> out;
        string why;

        if (!render1(pluginPath, unisonGraph((float)f0, 0, 10, 3, 0, 0.5f),
                     "string", "out", 256, (unsigned)(rate / 2), out, why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        const double fell = levelAt(out, f0, 0.25, 0.1) -
                            levelAt(out, f0, 0.05, 0.1);
        const double t60 = -60 * 0.2 / fell;
        const double want = 1 / (1 / 10.0 + 1 / 0.5);

        okOrFail(fabs(t60 / want - 1) < 0.1,
                 "filt::pianostring: three strings in tune fall 60 dB in "
                 "`prompt' and `decay' together",
                 "it took " + num(t60) + " s for " + num(want));
    }

    /* ---- an uneven blow leaves an aftersound in tune ---- */

    /* Two strings struck and heard 1.2 and 0.8: the in-phase part falls
       in `prompt' and `decay' together as above, and the 0.2 against it
       is the bridge-free motion, heard at 0.2 of that and falling in
       `decay' alone -- 4 dB a second for fifteen, exactly, since in tune
       it is a mode of its own. */
    {
        const double f0 = 262;
        vector<NodeSpec> spec = unisonGraph((float)f0, 0, 15, 2, 0, 0.5f);
        Value im = { "imbalance", 0.2f };
        vector<float> out;
        string why;

        spec[1].values.push_back(im);

        if (!render1(pluginPath, spec, "string", "out", 256,
                     (unsigned)(rate * 6), out, why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        const double early = (levelAt(out, f0, 0.25, 0.1) -
                              levelAt(out, f0, 0.05, 0.1)) / 0.2;
        const double late = (levelAt(out, f0, 4, 1) -
                             levelAt(out, f0, 2, 1)) / 2;

        okOrFail(early < -40 && fabs(late / -4 - 1) < 0.1,
                 "filt::pianostring: a tilted unison in tune "
                 "leaves motion the bridge does not take, falling in "
                 "`decay'",
                 "early " + num(early) + " dB/s, late " + num(late) +
                 " dB/s for -4");
    }

    /* ---- and the mistuning leaves an aftersound ---- */

    /* The same three strings a cent and a half apart, with `prompt' at a
       second and `decay' at fifteen. The prompt sound falls at about
       60 dB a second. What is left is mostly the strings moving against
       each other, which the bridge does not take, so it falls at least
       five times slower -- but not as slowly as `decay''s 4 dB a second
       alone, since a mistuned mode is never wholly out of phase and keeps
       leaking into the bridge (about 10 dB a second here). The late rate
       is over two-second windows three seconds apart, which average the
       beating out. In tune, the same graph has no aftersound at all --
       the control. */
    {
        static const float unisons[] = { 1.5f, 0 };
        const double f0 = 262;
        double late[2] = { 0, 0 }, early[2] = { 0, 0 };
        bool bad = false;

        for (size_t u = 0; u < 2 && !bad; u++)
        {
            vector<float> out;
            string why;

            if (!render1(pluginPath,
                         unisonGraph((float)f0, 0, 15, 3, unisons[u], 1),
                         "string", "out", 256, (unsigned)(rate * 8), out,
                         why))
            {
                fail("filt::pianostring renders", why);
                bad = true;
                break;
            }

            early[u] = (levelAt(out, f0, 0.3, 0.1) -
                        levelAt(out, f0, 0.05, 0.1)) / 0.25;
            late[u] = (levelAt(out, f0, 5, 2) - levelAt(out, f0, 2, 2)) / 3;
        }

        if (!bad)
        {
            okOrFail(early[0] < -20 && late[0] > early[0] / 5 &&
                     late[0] < -3.5,
                     "filt::pianostring: mistuned unison strings fall fast "
                     "and then slowly: the two-stage decay",
                     "early " + num(early[0]) + " dB/s, late " +
                     num(late[0]) + " dB/s");
            okOrFail(late[1] < -30,
                     "filt::pianostring: and in tune they only fall fast",
                     "late " + num(late[1]) + " dB/s");
        }
    }

    /* ---- the flattest string still fits its line ---- */

    /* The lowest note with the widest unison puts the outer strings a
       semitone either side of FREQ_MIN, the flat one below it. Uncoupled
       and eight seconds long, so the window resolves the 0.9 Hz between
       them. */
    {
        const double f0 = 16;
        const size_t from = (size_t)(rate / 20), n = (size_t)(rate * 8);
        vector<float> out;
        string why, detail;
        bool good = true;

        if (!render1(pluginPath, unisonGraph((float)f0, 0, 60, 3, 100, 0),
                     "string", "out", 256, (unsigned)(from + n), out, why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        for (int k = 0; k < 3 && good; k++)
        {
            const double want = f0 * pow(2.0, (k - 1) * 100 / 1200.0);
            const double got = peakNear(out, from, n, want, 30);

            if (fabs(cents(got, want)) > 2)
            {
                good = false;
                detail = "string " + num(k) + " at " + num(got) + " Hz for " +
                         num(want);
            }
        }

        okOrFail(good, "filt::pianostring: the widest unison on the lowest "
                       "note rings each string at its own pitch",
                 detail);
    }

    /* ---- strings rejoin the unison at rest ---- */

    /* Three strings struck, two of them dropped at 1.5 s and taken back at
       3 s. Falling 60 dB in three seconds, the one left is 30 dB below
       where the others stopped; had they kept what they held, taking
       them back would bring the note up by that much. */
    {
        const unsigned windowlen = 256;
        const unsigned drop = (unsigned)(rate * 1.5) / windowlen * windowlen;
        const unsigned back = (unsigned)(rate * 3) / windowlen * windowlen;
        const unsigned total = back + (unsigned)(rate / 2);
        const size_t span = (size_t)(rate / 10);
        thSynth synth(pluginPath, (int)windowlen, TH_DEFAULT_SAMPLES);
        thSynthTree tree("statecheck", &synth);
        vector<float> out;
        string why;

        if (!buildGraph(synth, tree, unisonGraph(262, 0, 3, 3, 0, 0), why))
        {
            fail("filt::pianostring renders", why);
            return;
        }

        thNode *str = tree.findNode("string");
        thArg *arg = str->getArg("out");

        for (unsigned done = 0; done < total; done += windowlen)
        {
            if (done == drop)
                str->setArg("strings", 1);
            else if (done == back)
                str->setArg("strings", 3);

            tree.setActiveNodes();
            tree.process(windowlen);

            for (unsigned i = 0; i < windowlen; i++)
                out.push_back((*arg)[i]);
        }

        double before = 0, after = 0;

        for (size_t i = 0; i < span; i++)
        {
            before += (double)out[back - span + i] * out[back - span + i];
            after += (double)out[back + i] * out[back + i];
        }

        const double rise = 10 * log10((after + 1e-30) / (before + 1e-30));

        okOrFail(rise < 1,
                 "filt::pianostring: strings taken back into the unison "
                 "start at rest",
                 "the note rose " + num(rise) + " dB");
    }

    windowsAgree(pluginPath,
                 unisonGraph(110, (float)pianoB(45), 10, 3, 1.5f, 1),
                 "string", "out",
                 "filt::pianostring: three coupled strings the same at one "
                 "sample a window and at five hundred");
}

/* ---- filt::pianostring's hammer ------------------------------------------ */

/* What the hammer claims:
 *
 *   - a harder blow is a shorter one and a brighter note -- the felt's
 *     nonlinearity, which nothing else in the node or the graph supplies;
 *   - it leaves: after the blow the force is 0 and stays there;
 *   - the strike point is a node of the partials it divides: struck an
 *     eighth of the way along, the 8th partial is missing;
 *   - it stays finite at every corner of its args;
 *   - and a window boundary is not an event.
 */

static vector<NodeSpec> hammerGraph (float freq, float strings, float velocity,
                                     float mass, float felt, float exponent,
                                     float position)
{
    vector<NodeSpec> spec = unisonGraph(freq, 0, 20, strings, 1, 8);
    Value v[] = { { "strike", 1 }, { "velocity", velocity },
                  { "mass", mass }, { "felt", felt },
                  { "exponent", exponent }, { "position", position } };

    /* No click: the hammer is the excitation. (`p = 0' would not do it:
       env::ad reads 0 as full scale.) */
    spec[1].wires.clear();

    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++)
        spec[1].values.push_back(v[i]);

    return spec;
}

/* Spectral centroid of `n' samples from `from', over `f0', from half the
   fundamental up. */
static double centroidOver (const vector<float> &v, size_t from, size_t n,
                            double f0)
{
    double num = 0, den = 0;

    for (double hz = f0 / 2; hz < 8000; hz += f0 / 8)
    {
        const double m = windowedMag(v, from, n, hz);

        num += m * m * hz;
        den += m * m;
    }

    return den > 0 ? num / den / f0 : 0;
}

static void checkHammer (const string &pluginPath)
{
    const double rate = TH_DEFAULT_SAMPLES;

    /* ---- a harder blow is shorter and brighter ---- */
    {
        static const float speeds[] = { 0.3f, 1 };
        double contact[2] = { 0, 0 }, bright[2] = { 0, 0 };
        bool bad = false;

        for (size_t v = 0; v < 2 && !bad; v++)
        {
            vector<Watch> watch;
            vector< vector<float> > got;
            string why;
            Watch w0 = { "string", "out" };
            Watch w1 = { "string", "force" };

            watch.push_back(w0);
            watch.push_back(w1);

            if (!render(pluginPath,
                        hammerGraph(261.63f, 3, speeds[v], 1.7f, 250, 2.5f,
                                    0.125f),
                        watch, 256, (unsigned)(rate / 2), got, why))
            {
                fail("filt::pianostring renders with a hammer", why);
                bad = true;
                break;
            }

            size_t first = got[1].size(), last = 0;

            for (size_t i = 0; i < got[1].size(); i++)
                if (got[1][i] > 0)
                {
                    if (first == got[1].size())
                        first = i;

                    last = i;
                }

            contact[v] = first < last ? (last - first) / rate * 1000 : 0;
            bright[v] = centroidOver(got[0], (size_t)(rate / 50),
                                     (size_t)(rate / 5), 261.63);
        }

        if (!bad)
            okOrFail(contact[0] > contact[1] && contact[1] > 0.5 &&
                     contact[0] < 10 && bright[1] > bright[0] * 1.1,
                     "filt::pianostring: a harder blow is a shorter one and "
                     "a brighter note",
                     "contact " + num(contact[0]) + " ms at 0.3, " +
                     num(contact[1]) + " ms at 1; centroid " +
                     num(bright[0]) + " and " + num(bright[1]) + " f0");
    }

    /* ---- it leaves ---- */
    {
        vector<float> force;
        string why;

        if (!render1(pluginPath,
                     hammerGraph(261.63f, 1, 0.7f, 1.7f, 250, 2.5f, 0.125f),
                     "string", "force", 256, (unsigned)(rate / 4), force, why))
            fail("filt::pianostring renders with a hammer", why);
        else
        {
            const size_t after = (size_t)(rate / 50);
            bool clear = true;

            for (size_t i = after; i < force.size(); i++)
                if (force[i] != 0)
                    clear = false;

            okOrFail(peak(force, 0) > 0 && clear,
                     "filt::pianostring: the hammer strikes and then is "
                     "clear of the string",
                     "peak force " + num(peak(force, 0)) +
                     (clear ? "" : ", still in contact after 20 ms"));
        }
    }

    /* ---- the strike point is a node ---- */

    /* One harmonic string, struck an eighth along: the 8th partial against
       the 7th and 9th either side of it. */
    {
        const double f0 = 220;
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     hammerGraph((float)f0, 1, 0.7f, 1.7f, 250, 2.5f, 0.125f),
                     "string", "out", 256, (unsigned)rate, out, why))
            fail("filt::pianostring renders with a hammer", why);
        else
        {
            const size_t from = (size_t)(rate / 10), n = (size_t)(rate / 2);
            const double m7 = windowedMag(out, from, n, 7 * f0);
            const double m8 = windowedMag(out, from, n, 8 * f0);
            const double m9 = windowedMag(out, from, n, 9 * f0);
            const double dip = 20 * log10(m8 / sqrt(m7 * m9));

            okOrFail(dip < -20,
                     "filt::pianostring: struck an eighth along, the 8th "
                     "partial is missing",
                     "the 8th is " + num(dip) + " dB against the 7th and 9th");
        }
    }

    /* ---- finite at every corner ---- */
    {
        static const float masses[] = { 0.01f, 10 };
        static const float felts[] = { 0.001f, 10000 };
        static const float exps[] = { 1, 5 };
        bool good = true;
        string detail;

        for (size_t a = 0; a < 2 && good; a++)
            for (size_t b = 0; b < 2 && good; b++)
                for (size_t c = 0; c < 2 && good; c++)
                    for (int note = 0; note < 2 && good; note++)
                    {
                        vector<float> out;
                        string why;
                        const float hz = note ? 4186 : 27.5f;

                        if (!render1(pluginPath,
                                     hammerGraph(hz, 3, 1, masses[a],
                                                 felts[b], exps[c], 0.5f),
                                     "string", "out", 256,
                                     (unsigned)(rate / 4), out, why))
                        {
                            good = false;
                            detail = why;
                        }
                        else if (!allFinite(out) || peak(out, 0) > 100)
                        {
                            good = false;
                            detail = num(hz) + " Hz, mass " +
                                     num(masses[a]) + ", felt " +
                                     num(felts[b]) + ", exponent " +
                                     num(exps[c]) + ": peak " +
                                     num(peak(out, 0));
                        }
                    }

        okOrFail(good, "filt::pianostring: the hammer stays finite at every "
                       "corner of mass, felt and exponent", detail);
    }

    /* ---- idle until the first blow ---- */

    /* `strike' held at 0 and the string rung through `in' instead: the
       hammer is never launched, so it must never touch the string -- no
       force, and the same sound as with no felt on it at all. */
    {
        vector< vector<float> > got[2];
        static const float felts[] = { 250, 0 };
        bool bad = false;

        for (size_t a = 0; a < 2 && !bad; a++)
        {
            vector<NodeSpec> spec = hammerGraph(261.63f, 3, 1, 1.7f, felts[a],
                                                2.5f, 0.125f);
            vector<Watch> watch;
            string why;
            Watch w0 = { "string", "out" };
            Watch w1 = { "string", "force" };
            Wire in = { "in", "src", "out" };

            for (size_t i = 0; i < spec[1].values.size(); i++)
                if (string(spec[1].values[i].arg) == "strike")
                    spec[1].values[i].value = 0;

            spec[1].wires.push_back(in);
            watch.push_back(w0);
            watch.push_back(w1);

            if (!render(pluginPath, spec, watch, 256, (unsigned)(rate / 10),
                        got[a], why))
            {
                fail("filt::pianostring renders with a hammer", why);
                bad = true;
            }
        }

        if (!bad)
            okOrFail(peak(got[0][1], 0) == 0 && peak(got[0][0], 0) > 0 &&
                     got[0][0] == got[1][0],
                     "filt::pianostring: the hammer is idle until `strike' "
                     "first rises",
                     "peak force " + num(peak(got[0][1], 0)) +
                     (got[0][0] == got[1][0] ? ""
                                             : ", and the felt changes the "
                                               "sound"));
    }

    windowsAgree(pluginPath,
                 hammerGraph(110, 3, 0.8f, 0.5f, 250, 2.5f, 0.125f),
                 "string", "out",
                 "filt::pianostring: the hammer the same at one sample a "
                 "window and at five hundred");
}

/* ---- filt::sympathetic -------------------------------------------------- */

/* What the node claims:
 *
 *   - a free string passes a sine at its own pitch at unity, whatever its
 *     decay, and a damped one next to nothing;
 *   - `pedal' is what frees them, and the keys from `undamped' up are free
 *     without it;
 *   - a free string rings at its key's pitch and falls in `decay' at middle
 *     C, shorter above;
 *   - and a window boundary is not an event.
 */

static vector<NodeSpec> sympatheticGraph (const NodeSpec &src, float low,
                                          float high, float pedal,
                                          float undamped, float decay,
                                          float damp)
{
    vector<NodeSpec> spec;
    NodeSpec bank;

    bank.name = "bank";
    bank.spelling = "filt/sympathetic";

    Value l = { "low", low };
    Value h = { "high", high };
    Value p = { "pedal", pedal };
    Value u = { "undamped", undamped };
    Value d = { "decay", decay };
    Value dp = { "damp", damp };
    Wire  in = { "in", "src", "out" };

    bank.values.push_back(l);
    bank.values.push_back(h);
    bank.values.push_back(p);
    bank.values.push_back(u);
    bank.values.push_back(d);
    bank.values.push_back(dp);
    bank.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(bank);

    return spec;
}

static NodeSpec sineSource (float hz)
{
    NodeSpec osc;

    osc.name = "src";
    osc.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value w = { "waveform", 0 };
    Value a = { "amp", 0.5f };

    osc.values.push_back(f);
    osc.values.push_back(w);
    osc.values.push_back(a);

    return osc;
}

static NodeSpec clickSource (void)
{
    NodeSpec src;

    src.name = "src";
    src.spelling = "env/ad";

    Value a = { "a", 0 };
    Value d = { "d", 1 };
    Value p = { "p", TH_MAX };

    src.values.push_back(a);
    src.values.push_back(d);
    src.values.push_back(p);

    return src;
}

static void checkSympathetic (const string &pluginPath)
{
    const double rate = TH_DEFAULT_SAMPLES;

    /* ---- a free string at unity, a damped one near nothing ---- */

    /* One string fed a sine at its own pitch for two seconds -- long past
       a two-second string's rise -- and the last tenth measured. The input
       peaks at 0.5, so unity is 0.5 out. Four cases: pedal down, pedal up,
       pedal up on a key above `undamped', and pedal up on that key with
       nothing undamped. A damped string still passes about `damper' over
       its free T60 of a sine at its pitch: a tenth of a second against two
       at A4, but against 0.8 at key 96, so that key's bound is looser --
       and still well under the 0.5 it passes when free. */
    {
        struct Case {
            float key, pedal, undamped;
            float most;             /* the damped bound; 0 for free */
            const char *what;
        };
        static const Case cases[] = {
            { 69, 1, 109, 0,     "with the pedal down a string at its own "
                                 "pitch passes it at unity" },
            { 69, 0, 90,  0.05f, "with the pedal up a key below "
                                 "`undamped' is damped" },
            { 96, 0, 90,  0,     "and a key above `undamped' is free with "
                                 "the pedal up" },
            { 96, 0, 109, 0.1f,  "and with nothing undamped the same key "
                                 "is damped" },
        };

        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            const double hz = 440 * pow(2.0, (cases[c].key - 69) / 12.0);
            vector<float> out;
            string why;

            if (!render1(pluginPath,
                         sympatheticGraph(sineSource((float)hz), cases[c].key,
                                          cases[c].key, cases[c].pedal,
                                          cases[c].undamped, 2, 0),
                         "bank", "out", 256, (unsigned)(rate * 2), out, why))
            {
                fail("filt::sympathetic renders", why);
                return;
            }

            const double got = peak(out, out.size() - (size_t)(rate / 10));

            okOrFail(cases[c].most == 0 ? fabs(got / 0.5 - 1) < 0.02
                                        : got < cases[c].most,
                     string("filt::sympathetic: ") + cases[c].what,
                     "peak " + num(got) + " for a 0.5 input");
        }
    }

    /* ---- at its pitch, and in `decay' ---- */

    /* A click into one free string: it rings at its key's pitch to a cent
       with `damp' in the loop, and falls 60 dB in `decay' at middle C and
       in 2^(-12/28) of it an octave up. */
    {
        static const int keys[] = { 60, 72 };

        for (size_t k = 0; k < 2; k++)
        {
            const double hz = 440 * pow(2.0, (keys[k] - 69) / 12.0);
            vector<float> out;
            string why;

            if (!render1(pluginPath,
                         sympatheticGraph(clickSource(), keys[k], keys[k], 1,
                                          109, 2, 0.3f),
                         "bank", "out", 256, (unsigned)(rate * 1.2), out,
                         why))
            {
                fail("filt::sympathetic renders", why);
                return;
            }

            const double pitch = peakNear(out, (size_t)(rate / 10),
                                          (size_t)rate, hz, 20);
            const double fell = levelAt(out, hz, 0.8, 0.1) -
                                levelAt(out, hz, 0.1, 0.1);
            const double t60 = -60 * 0.7 / fell;
            const double want = 2 * pow(2.0, (60 - keys[k]) / 28.0);

            okOrFail(fabs(cents(pitch, hz)) < 1 && fabs(t60 / want - 1) < 0.1,
                     "filt::sympathetic: key " + num(keys[k]) + " rings at "
                     "its pitch and falls 60 dB in " + num(want) + " s",
                     num(cents(pitch, hz)) + " cents, " + num(t60) + " s");
        }
    }

    /* The worst corner: `decay' two hundred seconds and `damp' as dark as
       it goes. Compensating the low-pass at a string's pitch lifts the
       loop's gain at DC toward one, and must stop short of it. In the top
       two octaves at this `damp' a string's own pitch is gone within half
       a second, and what is left is what each loop holds at DC: that has
       to fall, the last second quieter than the half second after the
       click. A loop gaining a part in two thousand a trip is over ten
       times louder by then. */
    {
        vector<float> out;
        string why;

        if (!render1(pluginPath,
                     sympatheticGraph(clickSource(), 84, 108, 1, 109, 200,
                                      0.95f),
                     "bank", "out", 256, (unsigned)(rate * 5), out, why))
            fail("filt::sympathetic renders", why);
        else
        {
            const vector<float> early(out.begin() + (size_t)(rate / 2),
                                      out.begin() + (size_t)rate);
            const double first = peak(early, 0);
            const double last = peak(out, (size_t)(rate * 4));

            okOrFail(allFinite(out) && last < first,
                     "filt::sympathetic: at `damp' 0.95 and `decay' 200 "
                     "the top strings fall rather than grow",
                     "0.5 to 1 s " + num(first) + ", last second " +
                     num(last));
        }
    }

    /* Eighty-eight lines, their low-passes and the pedal's follower all
       cross a window boundary. */
    {
        vector<NodeSpec> spec = sympatheticGraph(clickSource(), 21, 108, 1,
                                                 90, 8, 0.3f);

        windowsAgree(pluginPath, spec, "bank", "out",
                     "filt::sympathetic: the same bank at one sample a "
                     "window and at five hundred");
    }
}

/* ---- filt::vowel -------------------------------------------------------- */

/* The gain of a sine through the formants, RMS out over RMS in over a
   settled second. */
static double vowelGain (const string &pluginPath, float hz, float vowel,
                         float gender)
{
    vector<NodeSpec> spec;
    NodeSpec src, v;
    vector<Watch> watch;
    vector< vector<float> > got;
    string why;

    src.name = "src";
    src.spelling = "osc/simple";
    src.values.push_back(Value{ "freq", hz });
    src.values.push_back(Value{ "amp", 0.5f });
    src.values.push_back(Value{ "waveform", 0 });

    v.name = "v";
    v.spelling = "filt/vowel";
    v.values.push_back(Value{ "vowel", vowel });
    v.values.push_back(Value{ "gender", gender });
    v.wires.push_back(Wire{ "in", "src", "out" });

    spec.push_back(src);
    spec.push_back(v);
    watch.push_back(Watch{ "src", "out" });
    watch.push_back(Watch{ "v", "out" });

    if (!render(pluginPath, spec, watch, 256, TH_DEFAULT_SAMPLES / 2, got,
                why))
        return -1;

    return rms(got[1], TH_DEFAULT_SAMPLES / 10) /
           rms(got[0], TH_DEFAULT_SAMPLES / 10);
}

static void checkVowel (const string &pluginPath)
{
    /* ---- each vowel's first formant stands out ---- */

    /* A sine at each vowel's first formant against one midway to its
       second: the first is the formant table's loudest and narrowest, so
       it has to come out at about unity and at least four times the
       trough -- and the trough is exactly what tells an `a' from an `i'. */
    {
        static const float f1[] = { 650, 400, 290, 400, 350 };
        static const float f2[] = { 1080, 1700, 1870, 800, 600 };
        bool good = true;
        string detail;

        for (int v = 0; v < 5; v++)
        {
            const double at = vowelGain(pluginPath, f1[v], (float)v, 0);
            const double mid = vowelGain(pluginPath, (f1[v] + f2[v]) / 2,
                                         (float)v, 0);

            if (!(at > 0.8 && at < 1.5 && at > 4 * mid))
            {
                good = false;
                detail = "vowel " + num(v) + ": " + num(at) + " at " +
                         num(f1[v]) + " Hz, " + num(mid) + " at " +
                         num((f1[v] + f2[v]) / 2);
            }
        }

        okOrFail(good, "filt::vowel: each vowel passes its first formant at "
                       "about unity and a quarter as much between its first "
                       "two", detail);
    }

    /* ---- between two vowels, the formants move ---- */

    /* Halfway from `a' to `e' the first formant is halfway from 650 to 400,
       so 525 has to pass more than either end does. A filter that
       crossfaded two outputs instead would pass the ends and dip here. */
    {
        const double mid = vowelGain(pluginPath, 525, 0.5f, 0);
        const double a = vowelGain(pluginPath, 650, 0.5f, 0);
        const double e = vowelGain(pluginPath, 400, 0.5f, 0);

        okOrFail(mid > a && mid > e,
                 "filt::vowel: halfway from a to e the first formant is "
                 "halfway between theirs",
                 num(mid) + " at 525 Hz, " + num(a) + " at 650, " + num(e) +
                 " at 400");
    }

    /* ---- and `gender' moves all of them ---- */
    {
        const float up = 650 * powf(2, 0.25f);
        const double moved = vowelGain(pluginPath, up, 0, 1);
        const double left = vowelGain(pluginPath, 650, 0, 1);

        okOrFail(moved > 0.8 && moved > 1.5 * left,
                 "filt::vowel: `gender = 1' moves the first formant of `a' "
                 "a quarter octave up",
                 num(moved) + " at " + num(up) + " Hz, " + num(left) +
                 " at 650");
    }

    /* ---- finite at every corner ---- */
    {
        static const float vowels[] = { -5, 0, 2.5f, 4, 100 };
        static const float genders[] = { -10, 0, 10 };
        static const float freqs[] = { 20, 3000, 21000 };
        bool finite = true;
        string detail;

        for (float v : vowels)
            for (float g : genders)
                for (float hz : freqs)
                {
                    vector<NodeSpec> spec;
                    NodeSpec src, n;
                    vector<float> got;
                    string why;

                    src.name = "src";
                    src.spelling = "osc/simple";
                    src.values.push_back(Value{ "freq", hz });
                    src.values.push_back(Value{ "amp", TH_MAX });
                    src.values.push_back(Value{ "waveform", 2 });

                    n.name = "v";
                    n.spelling = "filt/vowel";
                    n.values.push_back(Value{ "vowel", v });
                    n.values.push_back(Value{ "gender", g });
                    n.wires.push_back(Wire{ "in", "src", "out" });

                    spec.push_back(src);
                    spec.push_back(n);

                    if (!render1(pluginPath, spec, "v", "out", 256, 8000, got,
                                 why))
                    {
                        fail("filt::vowel renders", why);
                        return;
                    }

                    if (finite && !(allFinite(got) && peak(got, 0) < 8))
                    {
                        finite = false;
                        detail = "vowel " + num(v) + ", gender " + num(g) +
                                 ", " + num(hz) + " Hz: peak " +
                                 num(peak(got, 0));
                    }
                }

        okOrFail(finite, "filt::vowel: finite and bounded at every corner",
                 detail);
    }

    {
        vector<NodeSpec> spec;
        NodeSpec src, n;

        src.name = "src";
        src.spelling = "osc/simple";
        src.values.push_back(Value{ "freq", 110 });
        src.values.push_back(Value{ "amp", 0.5f });
        src.values.push_back(Value{ "waveform", 1 });

        n.name = "v";
        n.spelling = "filt/vowel";
        n.values.push_back(Value{ "vowel", 1.3f });
        n.values.push_back(Value{ "gender", 0.4f });
        n.wires.push_back(Wire{ "in", "src", "out" });

        spec.push_back(src);
        spec.push_back(n);

        windowsAgree(pluginPath, spec, "v", "out",
                     "filt::vowel: the same formants at one sample a window "
                     "and at five hundred");
    }
}

/* ---- mixer::pan --------------------------------------------------------- */

static void checkPan (const string &pluginPath)
{
    /* A sine through the pan at five places, both sides watched: the two
       sides' powers have to sum to the input's at every sample -- which is
       the law, and is what a linear pan fails by half at the middle --
       with -1 all on the left, 1 all on the right, and the middle equal. */
    static const float places[] = { -1, -0.5f, 0, 0.3f, 1 };
    bool law = true, ends = true;
    string detail;

    for (float place : places)
    {
        vector<NodeSpec> spec;
        NodeSpec src, pan;
        vector<Watch> watch;
        vector< vector<float> > got;
        string why;

        src.name = "src";
        src.spelling = "osc/simple";
        src.values.push_back(Value{ "freq", 440 });
        src.values.push_back(Value{ "amp", 0.8f });
        src.values.push_back(Value{ "waveform", 0 });

        pan.name = "pan";
        pan.spelling = "mixer/pan";
        pan.values.push_back(Value{ "pan", place });
        pan.wires.push_back(Wire{ "in", "src", "out" });

        spec.push_back(src);
        spec.push_back(pan);
        watch.push_back(Watch{ "src", "out" });
        watch.push_back(Watch{ "pan", "out0" });
        watch.push_back(Watch{ "pan", "out1" });

        if (!render(pluginPath, spec, watch, 256, 4410, got, why))
        {
            fail("mixer::pan renders", why);
            return;
        }

        for (size_t i = 0; i < got[0].size(); i++)
        {
            const double in = got[0][i], l = got[1][i], r = got[2][i];

            if (law && fabs(l * l + r * r - in * in) > 1e-6)
            {
                law = false;
                detail = "at " + num(place) + ", sample " + num((double)i) +
                         ": " + num(l * l + r * r) + " against " +
                         num(in * in);
            }

            if (ends && ((place == -1 && (r != 0 || l != (float)in)) ||
                         (place == 1 && fabs(l) > 1e-6) ||
                         (place == 0 && l != r)))
            {
                ends = false;
                detail = "at " + num(place) + ": left " + num(l) +
                         ", right " + num(r);
            }
        }
    }

    okOrFail(law, "mixer::pan: the two sides' powers sum to the input's "
                  "wherever it is panned", detail);
    okOrFail(ends, "mixer::pan: -1 is the left alone, 1 the right, and 0 "
                   "the two equal", detail);
}

/* ---- dyn::compressor ----------------------------------------------------
 *
 * Three things a compressor has to be true about, and they are three
 * different measurements.
 *
 * The STATIC CURVE is what `threshold' and `ratio' mean: a steady signal so
 * many dB over the threshold comes out that many dB over, divided by the
 * ratio. Measured on a sine, well after the attack and with a release long
 * enough that the gain has settled.
 *
 * The ATTACK is what `attack' means: the 63% time of the gain's step. That
 * needs a step in the level rather than a note, so the input is a square
 * lifted by its own amplitude -- 0 for half a cycle and full scale for the
 * other half -- and what is watched is the `gain' output, in dB, where the
 * exponential is an exponential rather than something an envelope has
 * already shaped.
 *
 * The KEY is what `side' means: the same quiet signal, with and without a
 * loud key wired in. Only one of them ducks.
 */

/* `in' is a sine, and the compressor is the only other node. */
static vector<NodeSpec> compSineGraph (float amp, float hz, float threshold,
                                       float ratio, float attack,
                                       float release, float makeup,
                                       float knee)
{
    vector<NodeSpec> spec;
    NodeSpec src, comp;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", amp };
    Value w = { "waveform", 0 };           /* sine */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    comp.name = "comp";
    comp.spelling = "dyn/compressor";

    Value t = { "threshold", threshold };
    Value r = { "ratio", ratio };
    Value at = { "attack", attack };
    Value re = { "release", release };
    Value k = { "knee", knee };
    Value m = { "makeup", makeup };
    Wire  in = { "in", "src", "out" };

    comp.values.push_back(t);
    comp.values.push_back(r);
    comp.values.push_back(at);
    comp.values.push_back(re);
    comp.values.push_back(k);
    comp.values.push_back(m);
    comp.wires.push_back(in);

    spec.push_back(src);
    spec.push_back(comp);

    return spec;
}

/* A level step: a square at `hz' plus its own amplitude, so the signal is
 * silence for half a cycle and full scale for the other half. `keyed' puts
 * it on `side' and leaves `in' a quiet sine instead, which is the sidechain.
 */
static vector<NodeSpec> compStepGraph (float hz, float threshold, float ratio,
                                       float attack, float release,
                                       bool keyed, float quiet)
{
    vector<NodeSpec> spec;
    NodeSpec src, lift, quietSrc, comp;

    src.name = "src";
    src.spelling = "osc/simple";

    Value f = { "freq", hz };
    Value a = { "amp", (float)(TH_MAX * 0.5) };
    Value w = { "waveform", 2 };           /* square */

    src.values.push_back(f);
    src.values.push_back(a);
    src.values.push_back(w);

    lift.name = "lift";
    lift.spelling = "math/add";

    Value half = { "in1", (float)(TH_MAX * 0.5) };
    Wire  from = { "in0", "src", "out" };

    lift.values.push_back(half);
    lift.wires.push_back(from);

    quietSrc.name = "quiet";
    quietSrc.spelling = "osc/simple";

    Value qf = { "freq", 220 };
    Value qa = { "amp", quiet };
    Value qw = { "waveform", 0 };

    quietSrc.values.push_back(qf);
    quietSrc.values.push_back(qa);
    quietSrc.values.push_back(qw);

    comp.name = "comp";
    comp.spelling = "dyn/compressor";

    Value t = { "threshold", threshold };
    Value r = { "ratio", ratio };
    Value at = { "attack", attack };
    Value re = { "release", release };
    Value k = { "knee", 0 };

    comp.values.push_back(t);
    comp.values.push_back(r);
    comp.values.push_back(at);
    comp.values.push_back(re);
    comp.values.push_back(k);

    if (keyed)
    {
        Wire in = { "in", "quiet", "out" };
        Wire side = { "side", "lift", "out" };

        comp.wires.push_back(in);
        comp.wires.push_back(side);
    }
    else
    {
        Wire in = { "in", "lift", "out" };

        comp.wires.push_back(in);
    }

    spec.push_back(src);
    spec.push_back(lift);
    spec.push_back(quietSrc);
    spec.push_back(comp);

    return spec;
}

/* dB from full scale, which is the unit every number in this plugin is
   written in. */
static double dB (double linear)
{
    return linear > 1e-9 ? 20 * log10(linear) : -180;
}

static void checkCompressor (const string &pluginPath)
{
    const float rate = (float)TH_DEFAULT_SAMPLES;

    /* ---- the static curve --------------------------------------------- */

    /* 12 dB over a -20 dB threshold at 4:1 comes out 3 dB over. A long
     * release and a settled second half, because what is being measured is
     * where the gain ends up and not how it got there.
     */
    {
        const double want = -20 + 12.0 / 4;
        const float amp = (float)(TH_MAX * pow(10.0, -8.0 / 20.0));
        vector<float> got;
        string why;

        if (!render1(pluginPath,
                     compSineGraph(amp, 440, -20, 4, rate / 1000,
                                   rate / 5, 0, 0),
                     "comp", "out", 256, (unsigned)(rate / 2), got, why))
            fail("dyn::compressor renders", why);
        else
        {
            const double heard = dB(peak(got, got.size() / 2));

            okOrFail(allFinite(got) && fabs(heard - want) < 1,
                     "dyn::compressor: 12 dB over the threshold at 4:1 comes "
                     "out 3 dB over",
                     "wanted " + num(want) + " dB, heard " + num(heard));
        }
    }

    /* And under the threshold nothing happens at all -- the same graph, a
       quiet sine, out at the level it went in. Makeup is where an output
       that is not the input comes from, so it is here too. */
    {
        const float amp = (float)(TH_MAX * pow(10.0, -30.0 / 20.0));
        vector<float> plain, lifted;
        string why;

        if (!render1(pluginPath,
                     compSineGraph(amp, 440, -20, 4, rate / 1000, rate / 5,
                                   0, 0),
                     "comp", "out", 256, (unsigned)(rate / 4), plain, why) ||
            !render1(pluginPath,
                     compSineGraph(amp, 440, -20, 4, rate / 1000, rate / 5,
                                   6, 0),
                     "comp", "out", 256, (unsigned)(rate / 4), lifted, why))
            fail("dyn::compressor renders", why);
        else
        {
            const double quiet = dB(peak(plain, plain.size() / 2));
            const double loud = dB(peak(lifted, lifted.size() / 2));

            okOrFail(fabs(quiet - -30) < 0.5 && fabs(loud - quiet - 6) < 0.5,
                     "dyn::compressor: under the threshold it does nothing, "
                     "and `makeup' is added afterwards",
                     "in at -30 dB, out at " + num(quiet) + " dB, and " +
                     num(loud) + " dB with 6 dB of makeup");
        }
    }

    /* ---- the attack is the 63% time of the gain ------------------------ */

    /* A step from silence to full scale against a -20 dB threshold at 4:1 is
     * a reduction of 15 dB. One attack time after the step the gain has to
     * be 63% of the way there, and four of them all but arrived -- which is
     * what an exponential is, and is the claim `attack' makes on a panel.
     */
    {
        const float attack = rate / 100;           /* 10 ms */
        const double full = (1.0 / 4 - 1) * 20;    /* -15 dB */
        vector<float> gain;
        string why;

        if (!render1(pluginPath,
                     compStepGraph(4, -20, 4, attack, rate / 2, false, 0),
                     "comp", "gain", 256, (unsigned)(rate / 2), gain, why))
            fail("dyn::compressor renders", why);
        else
        {
            /* Where the square went up, found rather than assumed: the
               oscillator's phase at sample zero is its own business, and the
               first sample of a ten-millisecond attack has only moved a
               thirtieth of a dB -- so what is looked for is the gain
               *starting* to move, from a sample where it had let go. */
            long step = -1;

            for (size_t i = 1; i < gain.size() && step < 0; i++)
                if (gain[i] < -1e-3 && gain[i - 1] >= -1e-3)
                    step = (long)i;

            const size_t one = (size_t)(step + (long)attack);
            const size_t four = (size_t)(step + 4 * (long)attack);

            if (step < 0 || four >= gain.size())
                fail("dyn::compressor: the level step is in the window",
                     "step at " + num((double)step));
            else
            {
                const double at1 = gain[one] / full;
                const double at4 = gain[four] / full;

                okOrFail(fabs(at1 - 0.63) < 0.05 && at4 > 0.97 && at4 <= 1.01,
                         "dyn::compressor: `attack' is the 63% time of the "
                         "gain step, and four of them is arrival",
                         "one attack in " + num(at1 * 100) + "%, four in " +
                         num(at4 * 100) + "%");
            }
        }
    }

    /* The release is the same exponential the other way: the gain comes back
       to nothing once the key falls under the threshold. Measured at the end
       of the silent half cycle, which is many release times long. */
    {
        const float release = rate / 200;          /* 5 ms */
        vector<float> gain;
        string why;

        if (!render1(pluginPath,
                     compStepGraph(4, -20, 4, rate / 1000, release, false, 0),
                     "comp", "gain", 256, (unsigned)(rate / 2), gain, why))
            fail("dyn::compressor renders", why);
        else
        {
            double worst = 0, back = 0;

            /* The last sample before the square rises again, which is the
               end of a silent half cycle. */
            for (size_t i = 1; i < gain.size(); i++)
            {
                if (gain[i] < worst)
                    worst = gain[i];

                if (gain[i] < -1e-3 && gain[i - 1] >= -1e-3)
                    back = gain[i - 1];
            }

            okOrFail(worst < -14 && fabs(back) < 0.1,
                     "dyn::compressor: the gain lets go again once the key "
                     "is under the threshold",
                     "it pulled down " + num(worst) + " dB and came back to " +
                     num(back));
        }
    }

    /* ---- the key ------------------------------------------------------- */

    /* The same quiet sine, twice: once alone, and once with a full-scale key
     * on `side'. Only the second ducks, and it ducks by what the key is over
     * the threshold -- which is the whole of a sidechain.
     */
    {
        const float quiet = (float)(TH_MAX * pow(10.0, -24.0 / 20.0));
        const vector<NodeSpec> spec =
            compStepGraph(2, -20, 4, rate / 1000, rate / 200, true, quiet);
        vector<Watch> watch;
        vector< vector<float> > got;
        vector<float> alone;
        string why;

        Watch w0 = { "comp", "out" };
        Watch w1 = { "comp", "gain" };

        watch.push_back(w0);
        watch.push_back(w1);

        /* `quiet' out of the same graph is the signal before the compressor
           -- the control, measured rather than assumed. */
        if (!render1(pluginPath, spec, "quiet", "out", 256,
                     (unsigned)(rate / 2), alone, why) ||
            !render(pluginPath, spec, watch, 256, (unsigned)(rate / 2), got,
                    why))
            fail("dyn::compressor renders", why);
        else
        {
            const vector<float> &out = got[0];
            const vector<float> &gain = got[1];

            /* Which samples are "key up" and which are "key down" is the
               gain's own answer, so this does not depend on where the square
               happened to start. Settled samples only: the ramps between are
               the attack and the release, which the checks above are for. */
            double ducked = 0, open = 0;

            for (size_t i = 0; i < out.size() && i < gain.size(); i++)
            {
                if (gain[i] < -14.5 && fabs(out[i]) > ducked)
                    ducked = fabs(out[i]);

                if (gain[i] > -0.1 && fabs(out[i]) > open)
                    open = fabs(out[i]);
            }

            okOrFail(fabs(dB(peak(alone, 0)) - -24) < 0.5 &&
                     fabs(dB(open) - -24) < 0.5 &&
                     fabs(dB(ducked) - (-24 - 15)) < 1,
                     "dyn::compressor: a key on `side' ducks a quiet `in', "
                     "and lets go of it again",
                     "in at " + num(dB(peak(alone, 0))) + " dB, out at " +
                     num(dB(open)) + " dB with the key down and " +
                     num(dB(ducked)) + " dB with it up");
        }
    }

    /* ---- the knee ------------------------------------------------------ */

    /* A signal under the threshold and inside the knee. With a corner it is
     * untouched, because it is under the threshold; with the corner rounded
     * over 24 dB it is already being turned down a little, which is what a
     * soft knee is for -- the moment a compressor starts working is audible
     * where the curve has a corner in it, and not where it does not.
     */
    /* 3 dB under it rather than 6, so that the gate has room. The knee
     * spans the threshold, so over the 24 dB one here the reduction is
     * (1/ratio - 1) * (x - T + W/2)^2 / 2W -- which at 6 dB under is
     * 0.5625 dB, and a `more than half a dB' gate then passes on its
     * last digit. At 3 dB under it is 1.27 dB, and the same gate is
     * asserting the shape of the curve rather than the arithmetic's
     * rounding.
     */
    {
        const float amp = (float)(TH_MAX * pow(10.0, -23.0 / 20.0));
        vector<float> hard, soft;
        string why;

        if (!render1(pluginPath,
                     compSineGraph(amp, 440, -20, 4, rate / 1000, rate / 5,
                                   0, 0),
                     "comp", "out", 256, (unsigned)(rate / 4), hard, why) ||
            !render1(pluginPath,
                     compSineGraph(amp, 440, -20, 4, rate / 1000, rate / 5,
                                   0, 24),
                     "comp", "out", 256, (unsigned)(rate / 4), soft, why))
            fail("dyn::compressor renders", why);
        else
        {
            const double corner = dB(peak(hard, hard.size() / 2));
            const double rounded = dB(peak(soft, soft.size() / 2));

            okOrFail(fabs(corner - -23) < 0.5 &&
                     rounded < corner - 0.5 && rounded > corner - 3,
                     "dyn::compressor: a `knee' reaches under the threshold, "
                     "and a corner does not",
                     "3 dB under the threshold: " + num(corner) +
                     " dB with a corner, " + num(rounded) + " dB with 24 dB "
                     "of knee");
        }
    }

    /* And the state survives a window boundary, like every other stateful
       node here: the gain is a one-pole filter, and a one-pole filter that
       forgets where it was at the end of a window is a different sound at
       every buffer size. */
    windowsAgree(pluginPath,
                 compSineGraph((float)(TH_MAX * 0.5), 220, -20, 4,
                               rate / 1000, rate / 100, 0, 6),
                 "comp", "out",
                 "dyn::compressor: the same signal at one sample a window "
                 "and at five hundred");
}

int main (int argc, char **argv)
{
    string pluginPath = PLUGIN_PATH;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-p") && i + 1 < argc)
            pluginPath = argv[++i];

    if (pluginPath.empty() || pluginPath[pluginPath.size() - 1] != '/')
        pluginPath += '/';

    checkSlew(pluginPath);
    checkSvf(pluginPath);
    checkNoise(pluginPath);
    checkVibrato(pluginPath);
    checkAllpass(pluginPath);
    checkChorus(pluginPath);
    checkComb(pluginPath);
    checkPianostring(pluginPath);
    checkPianoUnison(pluginPath);
    checkHammer(pluginPath);
    checkSympathetic(pluginPath);
    checkPitchshift(pluginPath);
    checkFdn(pluginPath);
    checkFmop(pluginPath);
    checkSimple(pluginPath);
    checkSample(pluginPath);
    checkGrain(pluginPath);
    checkDrift(pluginPath);
    checkPad(pluginPath);
    checkVowel(pluginPath);
    checkPan(pluginPath);
    checkCompressor(pluginPath);

    printf("\n%d failure(s)\n", failed);

    return failed;
}
