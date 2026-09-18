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
#include <string>
#include <vector>

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

struct NodeSpec
{
    const char *name;
    const char *spelling;              /* "filt/svf" */
    vector<Value> values;
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

    printf("\n%d failure(s)\n", failed);

    return failed;
}
