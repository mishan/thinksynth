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
 * dspcompile -- a .dsp as a C++ struct: one voice of it, every node's
 * kernel (docs/KERNELS.md) run in the graph's order a sample at a time.
 *
 *   dspcompile [--engine] [-p plugins/] [-r rate] [-c device,...] file.dsp Name > thvName.h
 *
 * The patch is loaded as the engine loads it -- units folded, defaults
 * filled in -- and read off the graph: each input a number, a knob, the
 * note's, or another node's output. What depends on nothing but numbers,
 * knobs and the note is worked out once a call, as the engine works it out
 * once a window; the rest runs every sample. A plugin with no kernel here
 * is refused, by name, as is a loop in the graph. An effect's render()
 * takes its signal in, in[channel], as its io's in<N>; an effect reading
 * anything else the engine hands one (a sidechain, the pedal) is
 * refused.
 *
 * The rate is the compiler's, as it is the engine's at load: times written
 * in ms are samples at that rate by the time the graph can be read. The
 * engine keeps which node values were times (thSynthTree::nodeFolds), and
 * those the voice folds again at the rate bind() gives it, as the engine
 * would have at load; a knob's default and range stay at the compiler's.
 *
 * What comes out (thv<Name>, a voice):
 *
 *   struct Knobs { ... };          the patch's @controls, at their defaults
 *   void start (note, velocity, seed = 0);
 *                                  as the engine starts a voice; seed
 *                                  is its noises', one a voice
 *   void bind (rate);              to run at another rate
 *   void release ();               the trigger falls
 *   bool render (k, out, n[, play]);
 *                                  n samples into out[channel], and
 *                                  whether the voice still sounds; each
 *                                  sample of that into play, if given
 *
 * With each knob's range, set() turns a knob by name, for what a
 * controller's profile rides. With -c, the patch's controller block for
 * each device named over the device's own, checked against its controls,
 * is profile_DEVICE(): a thcHidBlock a thhMapper plays, so the firmware
 * needn't parse one.
 *
 * It renders what the engine renders, to the bit, for a voice whose knobs
 * hold still: scripts/compilecheck holds it to that.
 *
 * With --engine, thvName comes with thvNameEngine, what a tree runs in
 * its nodes' place (libthink/thCompiled.h), and a graph it couldn't run
 * as the engine does -- a knob's glide, the synth's noise -- is refused.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "think.h"

#include "thcHidBlock.h"
#include "thgLife.h"
#include "thgMarbles.h"
#include "thhBox.h"
#include "thhIndex.h"
#include "thhPlay.h"

using std::string;

/* A kernel the compiler can call: its control inputs, which make the
   struct its tick() takes and are worked out once a call while they're
   all control-rate, and the inputs its tick() takes a sample at a time,
   each in the order its call takes them. */
struct Kernel
{
    const char *plugin;
    std::vector<string> ctl;
    const char *ctlCall;        /* the control struct's maker, or NULL */
    bool ctlRate;               /* which takes the rate after them */
    std::vector<string> ins;
    bool pure;                  /* keeps nothing: control-rate if its inputs are */
    const char *type;           /* the state's struct, or NULL */
};

static const Kernel KERNELS[] = {
    { "osc/fmop", { "freq", "ratio", "feedback" }, "thkFmop::ctl", true,
      { "mod", "index", "reset" }, false, "thkFmop" },
    { "osc/simple", { "freq", "mul", "amp", "pw", "waveform" }, "thkSimple::ctl", true,
      { "fm", "fmamt", "reset" }, false, "thkSimple" },
    { "osc/blep", { "freq", "waveform", "pw" }, "thkBlep::ctl", true, { "phase", "reset" },
      false, "thkBlep" },
    { "osc/noise", { "color", "amp" }, "thkNoise::ctl", false, {}, false, "thkNoise" },
    { "filt/svf", { "cutoff", "res" }, "thkSvf::ctl", true, { "in" }, false, "thkSvf" },
    { "dist/saturate", {}, NULL, false, { "in", "factor" }, true, NULL },
    { "math/exp2", {}, NULL, false, { "in" }, true, NULL },
    { "math/pow", {}, NULL, false, { "base", "exp" }, true, NULL },
    { "env/ad", { "a", "d", "p" }, "thkAd::ctl", false, { "reset" }, false, "thkAd" },
    { "env/adsr", { "a", "d", "s", "r", "p" }, "thkAdsr", false, { "trigger", "reset" },
      false, NULL },
    { "misc/midi2freq", {}, NULL, false, { "note" }, true, NULL },
    { "mixer/pan", { "pan" }, "thkPan::ctl", false, { "in" }, true, NULL },
    { "math/clamp", {}, NULL, false, { "in", "lo", "hi" }, true, NULL },
    { "math/add", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/sub", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/mul", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/div", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "mixer/mul", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/sin", {}, NULL, false, { "index", "wavelength", "amp" }, true, NULL },
    { "math/max", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/min", {}, NULL, false, { "in0", "in1" }, true, NULL },
    { "math/abs", {}, NULL, false, { "in" }, true, NULL },
    { "misc/latch", {}, NULL, false, { "in", "latch" }, false, "thkLatch" },
    { "misc/scan", {}, NULL, false, {}, true, NULL },
    { "delay/allpass", {}, NULL, false, { "in", "delay", "gain" }, false, "thkAllpass" },
    { "delay/echo", {}, NULL, false,
      { "in", "size", "delay", "feedback", "dry", "tone", "low", "drive", "boost" }, false,
      "thkEcho" },
};

/* What a value changes with, past the call: where on the raster it is
   (its column, its row, its frame), or every sample. */
enum { D_COL = 1, D_ROW = 2, D_FRAME = 4, D_SAMPLE = 8 };

struct Value
{
    string text;
    bool   control;             /* the same at every sample of a call */
    int    deps = 0;            /* when it isn't: D_*, or none for every sample */
};

struct Compiler
{
    thSynthTree *tree;
    thNode *io;
    std::map<thNode *, int> order;          /* the node's number, once placed */
    std::vector<thNode *> placed;
    std::set<thNode *> placing;
    std::map<string, float> knobs;
    std::map<string, std::pair<float, float> > ranges;     /* min, max */
    std::map<string, std::pair<string, string> > knobAt;    /* a node.arg it feeds */
    std::map<thNode *, bool> control;
    std::map<thNode *, int> deps;
    bool scanned = false;                   /* a misc::scan read: a raster's */
    bool effect = false;                    /* its io's in<N>: a signal in */
    int width = 0;                          /* an effect's channels, at most */
    std::map<thNode *, bool> ctlOnce;       /* its control struct, once a call */
    std::map<thNode *, std::vector<Value> > ctls, ins;
    std::vector<thNodeFold> times;          /* each one bind() folds, t<i> */
    string error;

    /* By the plugin's path, less where it was found and its suffix. */
    static const Kernel *kernelFor (thNode *n)
    {
        if (!n->plugin())
            return NULL;

        string path = n->plugin()->path();
        const string suffix = PLUGIN_SUFFIX;

        if (path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), string::npos, suffix) == 0)
            path.resize(path.size() - suffix.size());
        for (const Kernel &k : KERNELS)
        {
            const string want = string("/") + k.plugin;

            if (path == k.plugin || (path.size() > want.size() &&
                                     path.compare(path.size() - want.size(),
                                                  string::npos, want) == 0))
                return &k;
        }
        return NULL;
    }

    /* As a .dsp spells it: osc::fmop, from wherever osc/fmop.so was. */
    static string spelled (thNode *n)
    {
        if (!n->plugin())
            return "a node with no plugin";

        string path = n->plugin()->path();
        const size_t slash = path.rfind('/'), dot = path.rfind('.');
        const size_t cat = slash == string::npos ? string::npos : path.rfind('/', slash - 1);

        path = path.substr(0, dot == string::npos || dot < slash ? string::npos : dot);
        path = path.substr(cat == string::npos ? 0 : cat + 1);
        return path.replace(path.find('/') == string::npos ? 0 : path.find('/'),
                            path.find('/') == string::npos ? 0 : 1, "::");
    }

    /* A knob's member: its own name might be C++'s (`or') or set()'s
       (`value', `name'). */
    static string knobField (const string &name) { return "k_" + name; }

    static string literal (float v)
    {
        char b[48];

        snprintf(b, sizeof(b), "%af", v);      /* hex: the float exactly */
        return b;
    }

    string var (thNode *n, const string &out)
    {
        return "n" + std::to_string(order[n]) + "_" + out;
    }

    /* What reaches `arg' of `n': a number, a knob, the note's, or a node's
       output, the node placed first. */
    bool value (thNode *n, thArg *a, Value &v)
    {
        for (int hops = 0; a && hops < 64; hops++)
        {
            switch (a->type())
            {
            case thArg::ARG_VALUE:
                if (a->len() > 1)
                {
                    error = n->name() + "." + a->name() + " is a list, which isn't compiled yet";
                    return false;
                }
                /* The last fold of an arg is what it holds: a time is the
                   voice's to fold again, what no rate changes is as
                   loaded. */
                for (auto f = tree->nodeFolds().rbegin(); f != tree->nodeFolds().rend(); ++f)
                    if (f->node == n->name() && f->arg == a->name())
                    {
                        if (f->units != "ms" && f->units != "s" && f->units != "beats")
                            break;
                        if (f->units == "beats")
                        {
                            error = n->name() + "." + a->name() + " is in beats, which "
                                    "follow a tempo a compiled voice isn't given";
                            return false;
                        }
                        /* One member an arg, however many reads reach it. */
                        size_t t = 0;

                        while (t < times.size() &&
                               !(times[t].node == f->node && times[t].arg == f->arg))
                            t++;
                        if (t == times.size())
                            times.push_back(*f);
                        v = { "t" + std::to_string(t), true };
                        return true;
                    }
                v = { literal(a->len() ? (*a)[0] : 0), true };
                return true;
            case thArg::ARG_CHANNEL:
            {
                thArg *knob = a->argPtr();

                knobs[a->argPtrName()] = knob && knob->len() ? (*knob)[0] : 0;
                knobAt.emplace(a->argPtrName(), std::make_pair(n->name(), a->name()));
                ranges[a->argPtrName()] = knob ? std::make_pair(knob->min(), knob->max())
                                               : std::make_pair(0.0f, 1.0f);
                v = { "k." + knobField(a->argPtrName()), true };
                return true;
            }
            case thArg::ARG_NOTE:
                v = { a->name(), true };
                return true;
            case thArg::ARG_POINTER:
            {
                thNode *to = tree->nodeAt(a->nodePtrId());
                const string &name = a->argPtrName();

                if (to == NULL)
                {
                    error = n->name() + "." + a->name() + " points at no node";
                    return false;
                }
                if (to == io && (name == "note" || name == "velocity" ||
                                 name == "trigger" || name.compare(0, 3, "aux") == 0))
                {
                    v = { name, true };
                    return true;
                }
                /* An effect's signal in, given to render() a sample at a
                   time: in<N>, one digit, as the engine writes a channel's
                   (thChanEffect), and no more than the effect's channels. */
                const bool digit = name.size() > 2 && isdigit((unsigned char)name.back());

                if (to == io && effect && name.size() == 3 && !name.compare(0, 2, "in") &&
                    digit)
                {
                    if (name[2] - '0' >= width)
                    {
                        error = n->name() + "." + a->name() + " reads " + name +
                                ", past the effect's " + std::to_string(width) + " channel" +
                                (width == 1 ? "" : "s");
                        return false;
                    }
                    v = { "in[" + name.substr(2) + "][i]", false, D_SAMPLE };
                    return true;
                }
                /* What else the engine hands an effect. */
                if (to == io && effect && (name == "pedal" ||
                    (digit && (!name.compare(0, 4, "side") || !name.compare(0, 4, "live") ||
                               !name.compare(0, 4, "send")))))
                {
                    error = n->name() + "." + a->name() + " reads the effect's " + name +
                            ", which isn't compiled yet";
                    return false;
                }
                /* Where the sample lands, as misc::scan works it out, from
                   the raster and the column and row render() steps. */
                if (to != io && kernelFor(to) && string(kernelFor(to)->plugin) == "misc/scan")
                {
                    static const struct { const char *out, *text; int deps; } SCAN[] = {
                        { "x", "((float)col / (float)W)", D_COL },
                        { "y", "((float)row / (float)H)", D_ROW },
                        { "hsync", "(col == 0 ? 1.0f : 0.0f)", D_COL },
                        { "vsync", "(col == 0 && row == 0 ? 1.0f : 0.0f)", D_SAMPLE },
                        { "frame", "((float)(pos / (W * H)))", D_FRAME },
                        { "t", "((float)((double)(pos - pos % (W * H)) / (double)boundRate))",
                          D_FRAME },
                        { "width", "((float)W)", 0 },
                        { "size", "((float)(W * H))", 0 },
                    };

                    scanned = true;
                    for (const auto &o : SCAN)
                        if (name == o.out)
                        {
                            v = { o.text, false, o.deps };
                            if (!o.deps)
                                v.control = true;
                            return true;
                        }
                    error = n->name() + "." + a->name() + " reads scan's " + name +
                            ", which it hasn't";
                    return false;
                }
                if (to != io && to->plugin() &&
                    to->plugin()->getArgDir(to->getArg(name) ? to->getArg(name)->index() : -1) ==
                    thPlugin::ARG_OUT)
                {
                    if (!place(to))
                        return false;
                    v = { var(to, name), control[to], deps[to] };
                    return true;
                }
                a = to->getArg(name);
                n = to;
                break;
            }
            default:
                error = n->name() + "." + a->name() + " is text, which isn't compiled";
                return false;
            }
        }
        error = n->name() + ": an arg that leads nowhere";
        return false;
    }

    /* The node after every node it reads, numbered in that order. */
    bool place (thNode *n)
    {
        if (order.count(n))
            return true;
        if (placing.count(n))
        {
            error = n->name() + " is in a loop, which isn't compiled yet";
            return false;
        }

        const Kernel *k = kernelFor(n);

        if (k == NULL)
        {
            error = n->name() + " is " + spelled(n) + ", which has no kernel yet";
            return false;
        }
        placing.insert(n);

        bool ctl = k->pure, once = true;
        int d = 0;
        const auto dep = [](const Value &v) {
            return v.control ? 0 : v.deps ? v.deps : (int)D_SAMPLE;
        };
        std::vector<Value> cs, vs;

        for (const string &in : k->ctl)
        {
            Value v;

            if (!value(n, n->getArg(in), v))
                return false;
            once = once && v.control;
            d |= dep(v);
            cs.push_back(v);
        }
        for (const string &in : k->ins)
        {
            Value v;

            if (!value(n, n->getArg(in), v))
                return false;
            ctl = ctl && v.control;
            d |= dep(v);
            vs.push_back(v);
        }
        placing.erase(n);
        ctls[n] = cs;
        ins[n] = vs;
        control[n] = ctl && once;
        deps[n] = control[n] ? 0 : k->pure ? d : (int)D_SAMPLE;
        ctlOnce[n] = once;
        order[n] = (int)placed.size();
        placed.push_back(n);
        return true;
    }

    /* A node's control struct, n<i>_ctl, as a statement. */
    string emitCtl (thNode *n)
    {
        const Kernel *k = kernelFor(n);
        string a;

        for (size_t i = 0; i < ctls[n].size(); i++)
            a += (i ? ", " : "") + ctls[n][i].text;
        if (k->ctlRate)
            a += ", runRate";
        return "        const auto " + var(n, "ctl") + " = " + k->ctlCall + "(" + a + ");\n";
    }

    /* The node's work, as statements; its outputs n<i>_out (and _play). */
    string emit (thNode *n)
    {
        const Kernel *k = kernelFor(n);
        const std::vector<Value> &v = ins[n];
        const string out = var(n, "out"), self = "n" + std::to_string(order[n]);
        const string p = string(k->plugin);
        std::ostringstream s;
        const auto args = [&](size_t from) {
            string a;

            for (size_t i = from; i < v.size(); i++)
                a += (i > from ? ", " : "") + v[i].text;
            return a;
        };

        s << "        /* " << n->name() << ", " << p << " */\n";
        if (k->ctlCall && !ctlOnce[n])
            s << emitCtl(n);
        if (p == "osc/fmop")
            s << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ", " << args(0) << ");\n";
        else if (p == "osc/simple")
            s << "        float " << var(n, "sync") << ";\n"
              << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ", " << args(0) << ", " << var(n, "sync") << ");\n";
        else if (p == "osc/blep")
            s << "        float " << var(n, "sync") << ", " << var(n, "edge") << ";\n"
              << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ", " << args(0) << ", " << var(n, "sync") << ", " << var(n, "edge")
              << ");\n";
        else if (p == "osc/noise")
            s << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ");\n";
        else if (p == "filt/svf")
            s << "        float " << var(n, "out_band") << ", " << var(n, "out_high") << ";\n"
              << "        const float " << var(n, "out_low") << " = " << self << ".tick("
              << var(n, "ctl") << ", " << args(0) << ", " << var(n, "out_band") << ", "
              << var(n, "out_high") << ");\n";
        else if (p == "dist/saturate")
            s << "        const float " << out << " = thkSaturate(" << args(0) << ");\n";
        else if (p == "math/exp2")
            s << "        const float " << out << " = exp2f(" << v[0].text << ");\n";
        else if (p == "math/pow")
            s << "        const float " << out << " = powf(" << args(0) << ");\n";
        else if (p == "env/ad" || p == "env/adsr")
            s << "        float " << var(n, "play") << ";\n"
              << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ", " << args(0) << ", " << var(n, "play") << ");\n"
              << "        (void)" << out << ";    /* an envelope read for play alone */\n";
        else if (p == "mixer/pan")
            s << "        const float " << var(n, "out0") << " = " << v[0].text << " * "
              << var(n, "ctl") << ".left;\n"
              << "        const float " << var(n, "out1") << " = " << v[0].text << " * "
              << var(n, "ctl") << ".right;\n";
        else if (p == "math/clamp")
            s << "        const float " << out << " = " << v[0].text << " < " << v[1].text
              << " ? " << v[1].text << " : " << v[0].text << " > " << v[2].text << " ? "
              << v[2].text << " : " << v[0].text << ";\n";
        else if (p == "math/sin")
            s << "        const float " << out << " = thkSin(" << args(0) << ");\n";
        else if (p == "math/max" || p == "math/min")
            s << "        const float " << out << " = (" << v[0].text
              << (p == "math/max" ? " > " : " < ") << v[1].text << ") ? " << v[0].text
              << " : " << v[1].text << ";\n";
        else if (p == "math/abs")
            s << "        const float " << out << " = fabsf(" << v[0].text << ");\n";
        else if (p == "delay/allpass")
            s << "        const unsigned int " << var(n, "len") << " = thkAllpassLen("
              << v[1].text << ", runRate);\n"
              << "        if (" << var(n, "len") << " != 0 && " << var(n, "len") << " != "
              << self << "_line.size())\n"
              << "            " << self << "_line.assign(" << var(n, "len") << ", 0);\n"
              << "        const float " << out << " = " << self << ".tick(" << self
              << "_line.data(), " << var(n, "len") << ", " << v[0].text << ", " << v[2].text
              << ");\n";
        else if (p == "delay/echo")
            s << "        const unsigned int " << var(n, "size") << " = thkEchoRing("
              << v[1].text << ", runRate);\n"
              << "        if (" << var(n, "size") << " != " << self << "_line.size())\n"
              << "            " << self << "_line.assign(" << var(n, "size") << ", 0);\n"
              << "        const float " << out << " = " << self << ".tick(" << self
              << "_line.data(), " << var(n, "size") << ", " << v[0].text << ", " << args(2)
              << ", runRate);\n";
        else if (p == "misc/latch")
            s << "        const float " << out << " = " << self << ".tick(" << args(0) << ");\n";
        else if (p == "misc/midi2freq")
            s << "        const float " << out << " = thkMidi2freq(" << v[0].text << ", runRate);\n";
        else
            s << "        const float " << out << " = " << v[0].text
              << (p == "math/add" ? " + " : p == "math/sub" ? " - " : p == "math/div" ? " / " : " * ")
              << v[1].text << ";\n";
        return s.str();
    }

    /* env::adsr runs free on a trigger written as a number, gated on one
       that moves: the plugin's own choice. */
    static string stateType (thNode *n)
    {
        const Kernel *k = kernelFor(n);

        if (k->type)
            return k->type;
        if (string(k->plugin) == "env/adsr")
        {
            const thArg *t = n->getArg("trigger");

            return !t || t->type() == thArg::ARG_VALUE ? "thkAdsrFree" : "thkAdsrGated";
        }
        return "";
    }
};

/* A binding, as statements filling `b'. */
static string
bindingCode (const thcHidBinding &b)
{
    std::ostringstream s;
    const auto f = [](float v) { return Compiler::literal(v); };

    s << "        b = thcHidBinding();\n"
      << "        b.kind = (thcHidBinding::Kind)" << (int)b.kind << ";\n"
      << "        b.control = \"" << b.control << "\";\n"
      << "        b.line = " << b.line << ";\n"
      << "        b.semis = " << b.semis << ";\n"
      << "        b.layer = \"" << b.layer << "\";\n"
      << "        b.arg = \"" << b.arg << "\";\n"
      << "        b.channel = " << b.channel << ";\n"
      << "        b.hasMin = " << b.hasMin << "; b.hasMax = " << b.hasMax << ";\n"
      << "        b.min = " << f((float)b.min) << "; b.max = " << f((float)b.max) << ";\n"
      << "        b.at = " << f((float)b.at) << "; b.off = " << f((float)b.off) << ";\n"
      << "        b.center = " << f((float)b.center) << "; b.smooth = " << f((float)b.smooth) << ";\n"
      << "        b.latch = " << b.latch << "; b.both = " << b.both << ";\n"
      << "        b.shape = (thcHidBinding::Shape)" << (int)b.shape << ";\n"
      << "        b.patch = " << b.patch << "; b.octaves = " << b.octaves << ";\n"
      << "        b.range = " << b.range << ";\n";
    for (int step : b.scale)
        s << "        b.scale.push_back(" << step << ");\n";
    return s.str();
}

/* The profile as a function building it. */
static string
profileCode (const thcHidBlock &p)
{
    std::ostringstream s;

    s << "\n    /* The patch's controller block for " << p.device << ", over its own. */\n"
      << "    static thcHidBlock profile_" << p.device << " ()\n    {\n"
      << "        thcHidBlock p;\n        thcHidBinding b;\n\n"
      << "        p.device = \"" << p.device << "\";\n"
      << "        p.channel = " << p.channel << ";\n"
      << "        p.base = " << p.base << ";\n"
      << "        p.velocity = " << p.velocity << ";\n";
    for (const thcHidBinding &b : p.bindings)
        s << bindingCode(b) << "        p.bindings.push_back(b);\n";
    for (const thcHidLayer &l : p.layers)
    {
        s << "        {\n        thcHidLayer l;\n\n"
          << "        l.name = \"" << l.name << "\";\n"
          << "        l.line = " << l.line << ";\n"
          << "        l.channel = " << l.channel << ";\n"
          << "        l.base = " << l.base << ";\n"
          << "        l.whenLine = " << l.whenLine << ";\n";
        for (const auto &w : l.when)
            s << "        l.when.push_back({ \"" << w.first << "\", "
              << Compiler::literal((float)w.second) << " });\n";
        for (const thcHidBinding &b : l.bindings)
            s << bindingCode(b) << "        l.bindings.push_back(b);\n";
        s << "        p.layers.push_back(l);\n        }\n";
    }
    s << "        return p;\n    }\n";
    return s.str();
}

/* What the engine runs in a tree's place (thCompiled.h), around the voice:
   its context, knobs and outputs found in the tree it is handed, and its
   delays' lines reserved, as the plugins reserve theirs, while the knobs
   that size them are still. Ends the voice's struct. */
static string
engineCode (Compiler &c, const string &name, const string &once, size_t outs,
            const string &fingerprint)
{
    const string self = "thv" + name;
    const size_t knobs = std::max<size_t>(c.knobs.size(), 1);
    std::ostringstream s, prime, lines;

    for (thNode *n : c.placed)
    {
        const string p = Compiler::kernelFor(n)->plugin;

        if (p != "delay/echo" && p != "delay/allpass")
            continue;

        const string line = "n" + std::to_string(c.order[n]) + "_line";
        const Value &size = c.ins[n][1];

        lines << "        e->v." << line << ".reserve(v." << line << ".capacity());\n";
        if (size.control)
            prime << "        " << line << ".reserve("
                  << (p == "delay/echo" ? "thkEchoRing(" : "thkAllpassLen(") << size.text
                  << ", runRate));\n";
    }
    s << "\n    /* Its lines' room, before the first window. */\n"
      << "    void prime (const Knobs &k)\n    {\n        (void)k;\n"
      << (prime.str().empty() ? "" : once) << prime.str();
    /* What once works out and the sizes don't read. */
    const std::regex declared("\\n *[A-Za-z_:]+(?: [A-Za-z_:]+)? (n[0-9]+_[a-z0-9_]+)(?: =|;)");

    for (std::sregex_iterator m(once.begin(), once.end(), declared), e;
         !prime.str().empty() && m != e; ++m)
        s << "        (void)" << (*m)[1] << ";\n";
    s << "    }\n};\n\n"
      << "struct " << self << "Engine : thCompiled\n{\n"
      << "    static const char *fingerprint (void)\n    {\n"
      << "        return R\"thfp(" << fingerprint << ")thfp\";\n    }\n\n"
      << "    static thCompiled *make (void) { return new " << self << "Engine; }\n\n"
      << "    " << self << " v;\n    " << self << "::Knobs k;\n"
      << "    int knobNode[" << knobs << "] = {}, knobArg[" << knobs << "] = {};\n"
      << "    int note = -1, velocity = -1, trigger = -1, aux[4] = { -1, -1, -1, -1 };\n"
      << "    int outs[" << outs << "] = {};\n    bool writes[" << outs << "] = {};\n"
      << "    int play = -1;\n";
    if (c.effect)
        s << "    int ins[" << c.width << "] = {};\n";
    s << "    std::vector<float> scratch;     /* what an out nobody reads is written to */\n\n"
      << "    thCompiled *clone (void) const override\n    {\n"
      << "        " << self << "Engine *e = new " << self << "Engine(*this);\n\n"
      << lines.str() << "        return e;\n    }\n\n"
      << "    bool bind (thSynthTree &tree, int channels) override\n    {\n"
      << "        thSynth *s = tree.synth();\n        thNode *io = tree.IONode();\n\n"
      << "        if (s == NULL || io == NULL || s->rasterWidth() <= 0 ||\n"
      << "            s->rasterHeight() <= 0 || channels != " << (c.effect ? c.width : 0)
      << ")\n            return false;\n"
      << "        v.bind(s->getSampleRate());\n";
    if (c.scanned)
        s << "        v.raster(s->rasterWidth(), s->rasterHeight());\n";
    s << "        note = io->getArg(\"note\")->index();\n"
      << "        velocity = io->getArg(\"velocity\")->index();\n"
      << "        trigger = io->getArg(\"trigger\")->index();\n"
      << "        for (int i = 0; i < 4; i++)\n        {\n"
      << "            thArg *a = io->getArg(\"aux\" + std::to_string(i));\n\n"
      << "            aux[i] = a ? a->index() : -1;\n        }\n"
      << "        for (int c = 0; c < " << outs << "; c++)\n        {\n"
      << "            const std::string out = \"out\" + std::to_string(c);\n\n"
      << "            if (io->getArg(out) == NULL)\n                return false;\n"
      << "            outs[c] = io->getArg(out)->index();\n"
      << "            writes[c] = thCompiledWrites(tree, out);\n        }\n";
    if (c.effect)
        s << "        for (int c = 0; c < " << c.width << "; c++)\n        {\n"
          << "            thArg *in = io->getArg(\"in\" + std::to_string(c));\n\n"
          << "            if (in == NULL)\n                return false;\n"
          << "            ins[c] = in->index();\n        }\n";
    else
        s << "        if (thCompiledWrites(tree, \"play\"))\n"
          << "            play = io->getArg(\"play\")->index();\n";
    int i = 0;

    for (const auto &k : c.knobs)
    {
        const auto &at = c.knobAt[k.first];

        s << "        {\n            thNode *n = tree.findNode(\"" << at.first << "\");\n"
          << "            thArg *a = n ? n->getArg(\"" << at.second << "\") : NULL;\n\n"
          << "            if (a == NULL)\n                return false;\n"
          << "            knobNode[" << i << "] = n->id();\n"
          << "            knobArg[" << i << "] = a->index();\n        }\n";
        i++;
    }
    s << "        scratch.assign((size_t)s->getWindowlen() * " << outs << ", 0);\n"
      << "        knobsOf(tree);\n        v.prime(k);\n        return true;\n    }\n\n"
      << "    void reset (void) override { v.start(0, 0); }\n\n"
      << "    void knobsOf (thSynthTree &tree)\n    {\n";
    i = 0;
    for (const auto &k : c.knobs)
    {
        s << "        {\n            thArg *a = tree.getArg(tree.nodeAt(knobNode[" << i
          << "]), knobArg[" << i << "]);\n\n"
          << "            k." << Compiler::knobField(k.first)
          << " = a && a->len() ? (*a)[0] : 0;\n        }\n";
        i++;
    }
    s << "        (void)tree;\n    }\n\n"
      << "    void render (thSynthTree &tree, unsigned int n) override\n    {\n"
      << "        thNode *io = tree.IONode();\n"
      << "        float *out[" << outs << "];\n\n"
      << "        v.note = (*io->getArg(note))[0];\n"
      << "        v.velocity = (*io->getArg(velocity))[0];\n"
      << "        v.trigger = (*io->getArg(trigger))[0];\n"
      << "        v.aux0 = aux[0] >= 0 ? (*io->getArg(aux[0]))[0] : 0;\n"
      << "        v.aux1 = aux[1] >= 0 ? (*io->getArg(aux[1]))[0] : 0;\n"
      << "        v.aux2 = aux[2] >= 0 ? (*io->getArg(aux[2]))[0] : 0;\n"
      << "        v.aux3 = aux[3] >= 0 ? (*io->getArg(aux[3]))[0] : 0;\n"
      << "        knobsOf(tree);\n"
      << "        for (int c = 0; c < " << outs << "; c++)\n"
      << "            out[c] = writes[c] ? tree.resolveIOArg(outs[c])->allocate(n)\n"
      << "                               : &scratch[(size_t)c * scratch.size() / " << outs
      << "];\n";
    if (c.effect)
    {
        s << "        const float *in[" << c.width << "];\n\n"
          << "        for (int c = 0; c < " << c.width << "; c++)\n"
          << "            in[c] = tree.resolveIOArg(ins[c])->values();\n";
    }
    s << "        v.render(k, " << (c.effect ? "in, " : "") << "out, (int)n, "
      << (c.scanned ? "tree.synth()->position(), " : "")
      << "play >= 0 ? tree.resolveIOArg(play)->allocate(n) : NULL);\n    }\n";
    return s.str();
}

int
main (int argc, char **argv)
{
    string plugins = "plugins/", devices;
    int rate = TH_DEFAULT_SAMPLES, at = 1;
    bool engine = false;

    for (; at + 1 < argc && argv[at][0] == '-'; at += 2)
        if (!strcmp(argv[at], "--engine"))
        {
            engine = true;
            at--;
        }
        else if (!strcmp(argv[at], "-p"))
            plugins = argv[at + 1];
        else if (!strcmp(argv[at], "-r"))
            rate = atoi(argv[at + 1]);
        else if (!strcmp(argv[at], "-c"))
            devices = argv[at + 1];
    if (argc - at != 2 || rate <= 0)
    {
        fprintf(stderr, "usage: %s [--engine] [-p plugins/] [-r rate] [-c device,...] "
                "file.dsp Name\n",
                argv[0]);
        return 2;
    }

    thSynth synth(plugins, TH_DEFAULT_WINDOW_LENGTH, rate);
    thSynthTree *tree = synth.loadTree(argv[at], 0, 100);
    const string name = argv[at + 1];

    if (tree == NULL || tree->IONode() == NULL)
    {
        fprintf(stderr, "%s: didn't load\n", argv[at]);
        return 1;
    }
    Compiler c = { tree, tree->IONode() };

    c.effect = tree->takesInput();
    /* The engine runs an effect on the fewer of its declared channels and
       its channel's; its outputs, here, stand in for the channel's. */
    while (c.io->getArg("out" + std::to_string(c.width)))
        c.width++;
    if (c.io->getArg("channels") && c.io->getArg("channels")->len())
        c.width = std::min(c.width, (int)(*c.io->getArg("channels"))[0]);
    std::vector<std::pair<string, Value> > outs;    /* io arg, its value */

    for (int ch = 0;; ch++)
    {
        const string arg = "out" + std::to_string(ch);
        Value v;

        if (!c.io->getArg(arg))
            break;
        if (!c.value(c.io, c.io->getArg(arg), v))
        {
            fprintf(stderr, "%s: %s\n", argv[at], c.error.c_str());
            return 1;
        }
        outs.push_back({ arg, v });
    }

    Value play = { "1", true };

    /* An effect has no note to end, whatever its io's play holds. */
    if (!c.effect && c.io->getArg("play") && !c.value(c.io, c.io->getArg("play"), play))
    {
        fprintf(stderr, "%s: %s\n", argv[at], c.error.c_str());
        return 1;
    }

    /* What the engine does that a graph compiled for it can't: draw from
       the synth's noise, and glide an arg a knob moves. */
    for (thNode *n : c.placed)
        if (engine && !strcmp(Compiler::kernelFor(n)->plugin, "osc/noise"))
        {
            fprintf(stderr, "%s: %s is an osc::noise, which a compiled graph can't draw from "
                    "the synth's stream\n", argv[at], n->name().c_str());
            return 1;
        }
        else
            for (const thNode::Smooth &sm : n->smoothing())
                if (engine && sm.control)
                {
                    fprintf(stderr, "%s: %s.%s glides when its knob moves, which a compiled "
                            "graph doesn't\n", argv[at], n->name().c_str(),
                            n->getArg(sm.index)->name().c_str());
                    return 1;
                }

    /* Each controller block, checked as the app checks one. */
    std::vector<thcHidBlock> profiles;

    for (size_t from = 0; from < devices.size();)
    {
        size_t comma = devices.find(',', from);
        const string device = devices.substr(from, comma == string::npos ? string::npos
                                                                         : comma - from);
        const thhControls *controls = device == "box" ? &thhBoxControls()
                                    : device == "index" ? &thhIndexControls()
                                    : device == "marbles" ? &thgMarblesControls()
                                    : device == "life" ? &thgLifeControls() : NULL;
        const char *own = device == "box" ? THH_BOX_PROFILE
                        : device == "marbles" ? THG_MARBLES_PROFILE
                        : device == "life" ? THG_LIFE_PROFILE : THH_INDEX_PROFILE;
        thcHidBlock profile;
        string err;

        from = comma == string::npos ? devices.size() : comma + 1;
        if (controls == NULL)
        {
            fprintf(stderr, "%s: no controller called '%s'\n", argv[at], device.c_str());
            return 1;
        }
        if (!thhLoadProfile("", *controls, own, "", profile, err,
                            tree->controller(device), argv[at]))
        {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        profiles.push_back(profile);
    }

    std::set<string> headers;

    for (thNode *n : c.placed)
        headers.insert(string(Compiler::kernelFor(n)->plugin));

    /* The kernels' headers are named for their plugins. */
    printf("/* %s as a compiled voice at %d Hz.\n   Written by dspcompile; do not edit. */\n\n#ifndef THV_%s_H\n#define THV_%s_H 1\n\n#include <math.h>\n#include <stdint.h>\n#include <string.h>\n\n#include \"thKernel.h\"\n",
           argv[at], rate, name.c_str(), name.c_str());
    for (const string &h : headers)
        if ((h.compare(0, 5, "math/") || h == "math/sin") && h != "mixer/mul" &&
            h != "misc/scan")
            printf("#include \"%s.h\"\n", h.c_str());
    if (c.scanned || headers.count("delay/allpass") || headers.count("delay/echo"))
        printf("\n#include <algorithm>\n#include <vector>\n");

    /* On a raster, each value by what it changes with: once a call, a table
       a line wide made once a call or once a frame, once a line, or every
       pixel. A node's statements are as they'd be every sample, so a value
       is the same bits wherever it's worked out. */
    string once, colTable, frameTable, lineDecl, line, tableLoads, pixel;
    std::vector<string> tables;
    const std::regex declared("const float (n[0-9]+_[a-z0-9_]+) = ");
    const auto names = [&](const string &t) {
        std::vector<string> v;

        for (std::sregex_iterator m(t.begin(), t.end(), declared), e; m != e; ++m)
            v.push_back((*m)[1]);
        return v;
    };
    const auto indent = [](string t, int by) {
        for (size_t i = 0; i < t.size(); i = t.find('\n', i) + 1)
            t.insert(i, string(by, ' '));
        return t;
    };

    for (thNode *n : c.placed)
    {
        const int d = c.deps[n];
        const string e = c.emit(n);

        if (Compiler::kernelFor(n)->ctlCall && c.ctlOnce[n])
            once += c.emitCtl(n);
        if (c.control[n])
            once += e;
        else if (d == D_COL || d == (D_COL | D_FRAME))
        {
            string &to = d == D_COL ? colTable : frameTable;

            to += indent(e, 4);
            for (const string &v : names(e))
            {
                tables.push_back(v);
                to += "            tab_" + v + "[col] = " + v + ";\n";
            }
        }
        else if (d && !(d & (D_COL | D_SAMPLE)))
        {
            for (const string &v : names(e))
                lineDecl += "        float " + v + " = 0;\n";
            line += indent(std::regex_replace(e, std::regex("const float "), ""), 8);
        }
        else
            pixel += indent(e, 4);
    }

    if (!profiles.empty())
        printf("#include \"thcHidBlock.h\"\n");
    if (engine)
        printf("#include \"think.h\"\n#include \"thCompiled.h\"\n");
    printf("\nstruct thv%s\n{\n    static constexpr float rate = %d;\n"
           "    static constexpr int channels = %zu;\n\n"
           "    struct Knobs\n    {\n", name.c_str(), rate, outs.size());
    for (const auto &k : c.knobs)
        printf("        float %s = %s;\n", Compiler::knobField(k.first).c_str(),
               Compiler::literal(k.second).c_str());
    printf("\n        /* A knob by name, and its range, where the patch gave one:\n"
           "           false for a knob the patch hasn't got. */\n"
           "        bool set (const char *name, float value)\n        {\n");
    for (const auto &k : c.knobs)
        printf("            if (!strcmp(name, \"%s\")) { %s = value; return true; }\n",
               k.first.c_str(), Compiler::knobField(k.first).c_str());
    printf("            return false;\n        }\n\n"
           "        static bool range (const char *name, float &min, float &max)\n        {\n");
    for (const auto &r : c.ranges)
        printf("            if (!strcmp(name, \"%s\")) { min = %s; max = %s; return true; }\n",
               r.first.c_str(), Compiler::literal(r.second.first).c_str(),
               Compiler::literal(r.second.second).c_str());
    printf("            return false;\n        }\n    };\n\n"
           "    float note = 0, velocity = 0, trigger = 0;\n"
           "    float aux0 = 0, aux1 = 0, aux2 = 0, aux3 = 0;\n");
    for (thNode *n : c.placed)
        if (!Compiler::stateType(n).empty())
        {
            printf("    %s n%d;    /* %s */\n", Compiler::stateType(n).c_str(), c.order[n],
                   n->name().c_str());
            /* A delay's line, which the voice keeps as the plugin keeps its
               state arg: sized, and zeroed, when its length changes. */
            if (!strncmp(Compiler::kernelFor(n)->plugin, "delay/", 6))
                printf("    std::vector<float> n%d_line;\n", c.order[n]);
        }

    /* A time is folded as the engine folds it at load (thFoldUnit). */
    const auto fold = [&](const thNodeFold &f, const char *r) {
        return "(float)((double)" + Compiler::literal(f.literal) + " * (double)" + r +
               (f.units == "ms" ? " / 1000.0)" : ")");
    };

    printf("\n    /* The rate the voice runs at, rate unless bind() says otherwise,\n"
           "       and each time the patch wrote, in samples at it. */\n"
           "    long boundRate = (long)rate;\n    float runRate = rate;\n");
    for (size_t i = 0; i < c.times.size(); i++)
        printf("    float t%zu = %s;    /* %s.%s, %g %s */\n", i,
               fold(c.times[i], "(long)rate").c_str(), c.times[i].node.c_str(),
               c.times[i].arg.c_str(), c.times[i].literal, c.times[i].units.c_str());
    printf("\n    /* To run at `r' samples a second, the knobs' defaults and ranges\n"
           "       still at rate's. Kept by start(). */\n"
           "    void bind (long r)\n    {\n"
           "        boundRate = r;\n        runRate = (float)r;\n");
    for (size_t i = 0; i < c.times.size(); i++)
        printf("        t%zu = %s;\n", i, fold(c.times[i], "r").c_str());
    printf("    }\n");

    if (c.scanned)
    {
        printf("\n    /* The raster, `w' by `h', which a voice must be given before it\n"
               "       renders. Kept by start(). */\n"
               "    int64_t W = 0, H = 0;\n");
        for (const string &v : tables)
            printf("    std::vector<float> tab_%s;\n", v.c_str());
        printf("\n    void raster (int64_t w, int64_t h)\n    {\n"
               "        W = w;\n        H = h;\n");
        for (const string &v : tables)
            printf("        tab_%s.assign((size_t)w, 0);\n", v.c_str());
        printf("    }\n");
    }

    /* start() keeps the raster, and the memory of its tables and its
       delays' lines: a note-on that allocates isn't one a firmware can
       take. render() fills a table before reading it, and a line is
       zeroed here as a new voice's is. */
    string keep, restore;
    std::vector<string> kept;

    if (c.scanned)
    {
        keep = "        const int64_t w = W, h = H;\n";
        restore = "        W = w;\n        H = h;\n";
        for (const string &v : tables)
            kept.push_back("tab_" + v);
    }
    for (thNode *n : c.placed)
        if (!strncmp(Compiler::kernelFor(n)->plugin, "delay/", 6))
            kept.push_back("n" + std::to_string(c.order[n]) + "_line");
    for (const string &v : kept)
    {
        keep += "        std::vector<float> keep_" + v + " = std::move(" + v + ");\n";
        restore += "        " + v + " = std::move(keep_" + v + ");\n";
        if (v.compare(0, 4, "tab_"))
            restore += "        std::fill(" + v + ".begin(), " + v + ".end(), 0.0f);\n";
    }

    printf("\n    /* As the engine starts a voice: velocity 0..127 to 0..1; each noise\n"
           "       its own stream, from seed. Not the engine's streams, which come\n"
           "       from the synth's; and one stream for every voice given the same\n"
           "       seed, so a firmware gives each voice its own. */\n"
           "    void start (float n, float v, uint32_t seed = 0)\n    {\n"
           "        const long r = boundRate;\n%s\n"
           "        *this = thv%s();\n"
           "        bind(r);\n%s"
           "        note = n;\n        velocity = v * %s / %d;\n        trigger = 1;\n",
           keep.c_str(), name.c_str(), restore.c_str(),
           Compiler::literal(TH_MAX).c_str(), MIDIVALMAX);
    for (thNode *n : c.placed)
        if (Compiler::stateType(n) == "thkNoise")
            printf("        n%d.seed(seed * 2654435761u + %du);\n", c.order[n], c.order[n] + 1);
    printf("        (void)seed;\n    }\n\n"
           "    void release () { trigger = 0; }\n\n");
    /* An effect's signal in, a buffer a channel, read as out is written. */
    const char *inArg = c.effect ? "const float *const *in, " : "";

    if (c.scanned)
    {
        /* `at' is the raster's sample the call begins on, as misc::scan
           reads the synth's position. */
        /* Each table loaded where what follows reads it. */
        const auto reads = [](const string &text, const string &v) {
            return std::regex_search(text, std::regex("\\b" + v + "\\b"));
        };
        string after;

        for (const auto &o : outs)
            after += o.second.text + "\n";
        after += pixel + play.text;

        string colLoads, frameLoads;

        for (const string &v : tables)
        {
            const string load = "            const float " + v + " = tab_" + v + "[col];\n";
            const bool col = colTable.find("tab_" + v + "[col] =") != string::npos;

            if (reads(after, v))
                (col ? colLoads : frameLoads) += load;
            if (col && reads(frameTable, v))
                tableLoads += load;
        }
        printf("    bool render (const Knobs &k, %sfloat *const *out, int count, int64_t at,\n"
               "                 float *play = NULL)\n"
               "    {\n        (void)k;\n        float sounding = 1;\n\n"
               "        /* With no raster, nothing: misc::scan's zeros are no picture. */\n"
               "        if (W <= 0 || H <= 0)\n        {\n"
               "            for (int c = 0; c < channels; c++)\n"
               "                memset(out[c], 0, count * sizeof(float));\n"
               "            if (play)\n                memset(play, 0, count * sizeof(float));\n"
               "            return false;\n        }\n%s\n"
               "        for (int64_t col = 0; col < W; col++)\n        {\n%s        }\n%s"
               "\n        int64_t col = at %% W, row = at / W %% H%s;\n\n"
               "        for (int i = 0; i < count; i++)\n        {\n"
               "            const int64_t pos = at + i;\n\n"
               "            (void)pos;\n"
               "            if (i > 0 && ++col == W)\n            {\n"
               "                col = 0;\n                if (++row == H)\n"
               "                    row = 0;\n            }\n"
               "            if (i == 0 || col == 0)\n            {\n%s%s            }\n%s%s",
               inArg, once.c_str(), indent(colTable, 0).c_str(), lineDecl.c_str(),
               frameTable.empty() ? "" : ", frameOf = -1", line.c_str(),
               frameTable.empty() ? ""
               : ("                if (pos / (W * H) != frameOf)\n                {\n"
                  "                    frameOf = pos / (W * H);\n"
                  "                    for (int64_t col = 0; col < W; col++)\n"
                  "                    {\n" + indent(tableLoads, 12) + indent(frameTable, 12) +
                  "                    }\n                }\n").c_str(),
               (colLoads + frameLoads).c_str(), pixel.c_str());
    }
    else
        printf("    bool render (const Knobs &k, %sfloat *const *out, int count,\n"
               "                 float *play = NULL)\n    {\n"
               "        (void)k;\n        float sounding = 1;\n\n%s"
               "\n        for (int i = 0; i < count; i++)\n        {\n%s",
               inArg, once.c_str(), pixel.c_str());
    for (size_t ch = 0; ch < outs.size(); ch++)
        printf("            out[%zu][i] = %s;\n", ch, outs[ch].second.text.c_str());
    printf("            sounding = %s;\n            if (play)\n                play[i] = sounding;\n"
           "        }\n        return sounding > 0;\n    }\n",
           play.text.c_str());
    for (const thcHidBlock &p : profiles)
        printf("%s", profileCode(p).c_str());
    if (engine)
        printf("%s", engineCode(c, name, once, outs.size(), tree->fingerprint()).c_str());
    printf("};\n\n#endif\n");
    return 0;
}
