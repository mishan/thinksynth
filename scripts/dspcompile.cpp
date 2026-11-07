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
 *   dspcompile [-p plugins/] [-r rate] file.dsp Name > thvName.h
 *
 * The patch is loaded as the engine loads it -- units folded, defaults
 * filled in -- and read off the graph: each input a number, a knob, the
 * note's, or another node's output. What depends on nothing but numbers,
 * knobs and the note is worked out once a call, as the engine works it out
 * once a window; the rest runs every sample. A plugin with no kernel here
 * is refused, by name, as is a loop in the graph.
 *
 * The rate is the compiler's, as it is the engine's at load: times written
 * in ms are samples at that rate by the time the graph can be read.
 *
 * What comes out (thv<Name>, a voice):
 *
 *   struct Knobs { ... };          the patch's @controls, at their defaults
 *   void start (note, velocity);   as the engine starts a voice
 *   void release ();               the trigger falls
 *   bool render (k, out, n);       n samples into out[channel], and
 *                                  whether the voice still sounds
 *
 * It renders what the engine renders, to the bit, for a voice whose knobs
 * hold still: scripts/compilecheck holds it to that.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "think.h"

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
    std::vector<string> ins;
    bool pure;                  /* keeps nothing: control-rate if its inputs are */
    const char *type;           /* the state's struct, or NULL */
};

static const Kernel KERNELS[] = {
    { "osc/fmop", { "freq", "ratio", "feedback" }, "thkFmop::ctl", { "mod", "index", "reset" },
      false, "thkFmop" },
    { "env/ad", { "a", "d", "p" }, "thkAd::ctl", { "reset" }, false, "thkAd" },
    { "env/adsr", { "a", "d", "s", "r", "p" }, "thkAdsr", { "trigger", "reset" }, false, NULL },
    { "misc/midi2freq", {}, NULL, { "note" }, true, NULL },
    { "math/add", {}, NULL, { "in0", "in1" }, true, NULL },
    { "math/sub", {}, NULL, { "in0", "in1" }, true, NULL },
    { "math/mul", {}, NULL, { "in0", "in1" }, true, NULL },
    { "mixer/mul", {}, NULL, { "in0", "in1" }, true, NULL },
};

struct Value
{
    string text;
    bool   control;             /* the same at every sample of a call */
};

struct Compiler
{
    thSynthTree *tree;
    thNode *io;
    std::map<thNode *, int> order;          /* the node's number, once placed */
    std::vector<thNode *> placed;
    std::set<thNode *> placing;
    std::map<string, float> knobs;
    std::map<thNode *, bool> control;
    std::map<thNode *, bool> ctlOnce;       /* its control struct, once a call */
    std::map<thNode *, std::vector<Value> > ctls, ins;
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
                v = { literal(a->len() ? (*a)[0] : 0), true };
                return true;
            case thArg::ARG_CHANNEL:
            {
                thArg *knob = a->argPtr();

                knobs[a->argPtrName()] = knob && knob->len() ? (*knob)[0] : 0;
                v = { "k." + a->argPtrName(), true };
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
                if (to != io && to->plugin() &&
                    to->plugin()->getArgDir(to->getArg(name) ? to->getArg(name)->index() : -1) ==
                    thPlugin::ARG_OUT)
                {
                    if (!place(to))
                        return false;
                    v = { var(to, name), control[to] };
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
        std::vector<Value> cs, vs;

        for (const string &in : k->ctl)
        {
            Value v;

            if (!value(n, n->getArg(in), v))
                return false;
            once = once && v.control;
            cs.push_back(v);
        }
        for (const string &in : k->ins)
        {
            Value v;

            if (!value(n, n->getArg(in), v))
                return false;
            ctl = ctl && v.control;
            vs.push_back(v);
        }
        placing.erase(n);
        ctls[n] = cs;
        ins[n] = vs;
        control[n] = ctl;
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
        if (string(k->plugin) == "osc/fmop")
            a += ", rate";
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
        else if (p == "env/ad" || p == "env/adsr")
            s << "        float " << var(n, "play") << ";\n"
              << "        const float " << out << " = " << self << ".tick(" << var(n, "ctl")
              << ", " << args(0) << ", " << var(n, "play") << ");\n";
        else if (p == "misc/midi2freq")
            s << "        const float " << out << " = thkMidi2freq(" << v[0].text << ", rate);\n";
        else
            s << "        const float " << out << " = " << v[0].text
              << (p == "math/add" ? " + " : p == "math/sub" ? " - " : " * ") << v[1].text << ";\n";
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

int
main (int argc, char **argv)
{
    string plugins = "plugins/";
    int rate = TH_DEFAULT_SAMPLES, at = 1;

    for (; at + 1 < argc && argv[at][0] == '-'; at += 2)
        if (!strcmp(argv[at], "-p"))
            plugins = argv[at + 1];
        else if (!strcmp(argv[at], "-r"))
            rate = atoi(argv[at + 1]);
    if (argc - at != 2 || rate <= 0)
    {
        fprintf(stderr, "usage: %s [-p plugins/] [-r rate] file.dsp Name\n", argv[0]);
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

    if (c.io->getArg("play") && !c.value(c.io, c.io->getArg("play"), play))
    {
        fprintf(stderr, "%s: %s\n", argv[at], c.error.c_str());
        return 1;
    }

    std::set<string> headers;

    for (thNode *n : c.placed)
        headers.insert(string(Compiler::kernelFor(n)->plugin));

    /* The kernels' headers are named for their plugins. */
    printf("/* %s as a compiled voice at %d Hz.\n   Written by dspcompile; do not edit. */\n\n#ifndef THV_%s_H\n#define THV_%s_H 1\n\n#include \"thKernel.h\"\n",
           argv[at], rate, name.c_str(), name.c_str());
    for (const string &h : headers)
        if (h.compare(0, 5, "math/") && h != "mixer/mul")
            printf("#include \"%s.h\"\n", h.c_str());

    printf("\nstruct thv%s\n{\n    static constexpr float rate = %d;\n\n"
           "    struct Knobs\n    {\n", name.c_str(), rate);
    for (const auto &k : c.knobs)
        printf("        float %s = %s;\n", k.first.c_str(), Compiler::literal(k.second).c_str());
    printf("    };\n\n    float note = 0, velocity = 0, trigger = 0;\n");
    for (thNode *n : c.placed)
        if (!Compiler::stateType(n).empty())
            printf("    %s n%d;    /* %s */\n", Compiler::stateType(n).c_str(), c.order[n],
                   n->name().c_str());

    printf("\n    /* As the engine starts a voice: velocity 0..127 to 0..1. */\n"
           "    void start (float n, float v)\n    {\n"
           "        note = n;\n        velocity = v * %s / %d;\n        trigger = 1;\n    }\n\n"
           "    void release () { trigger = 0; }\n\n"
           "    bool render (const Knobs &k, float *const *out, int count)\n    {\n"
           "        (void)k;\n        float sounding = 1;\n\n",
           Compiler::literal(TH_MAX).c_str(), MIDIVALMAX);

    /* Once a call: what numbers, knobs and the note alone decide, and the
       control structs of kernels whose control inputs are all that. */
    for (thNode *n : c.placed)
        if (c.control[n])
            printf("%s", c.emit(n).c_str());
        else if (Compiler::kernelFor(n)->ctlCall && c.ctlOnce[n])
            printf("%s", c.emitCtl(n).c_str());
    printf("\n        for (int i = 0; i < count; i++)\n        {\n");
    for (thNode *n : c.placed)
        if (!c.control[n])
        {
            string s = c.emit(n);

            for (size_t i = 0; i < s.size(); i = s.find('\n', i) + 1)
                s.insert(i, "    ");
            printf("%s", s.c_str());
        }
    for (size_t ch = 0; ch < outs.size(); ch++)
        printf("            out[%zu][i] = %s;\n", ch, outs[ch].second.text.c_str());
    printf("            sounding = %s;\n        }\n        return sounding > 0;\n    }\n};\n\n"
           "#endif\n", play.text.c_str());
    return 0;
}
