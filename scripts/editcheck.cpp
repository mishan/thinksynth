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

/* editcheck -- a piece's text replaced while it plays (thcGenDiff).
 *
 * Two peers per seeded piece, each with its own synth and scheduler: one
 * stepped in windows of 1024 at 44.1 kHz, the other in jittered steps of
 * 2 to 60 ms. Both are handed the same edits at the same transport times --
 * times on no grid, since what makes an edit agree between peers is that it
 * lands at one transport time and not that the time is a round one:
 *
 *   A  a comment, which changes no stage;
 *   B  one numeric param of one stage of the last chain;
 *   C  a chain added, on channel 16, which nothing else plays on;
 *   D  that chain taken away again.
 *
 * What has to hold:
 *
 *   0. With no edit, the two step sizes deliver one tape -- so that a
 *      failure below is the edit's and not the piece's.
 *   1. The two peers deliver one tape, through every edit.
 *   2. The channels only unedited chains play on deliver the tape of a run
 *      with no edits at all: a stage whose text did not change kept its
 *      instance, its wake and its state, whatever happened around it.
 *   3. The added chain is heard from its edit on and not before, and is
 *      silent once it is taken away -- apart from what it had already
 *      composed, which is delivered as composed.
 *   4. A rewind after the edits plays what a fresh load of the final text
 *      plays: the document is the new text, and a rewind is a load.
 *   5. A knob whose declaration an edit did not touch keeps the value it
 *      was moved to; one whose declaration changed takes the new value.
 *   6. A text that does not load changes nothing.
 *   7. Edits to small texts, each after what a whole piece cannot show: a
 *      chain's start, its history, a knob's metadata, a device, a removed
 *      chain's queue, a held key, the tempo, where a rewind puts a knob a
 *      chain writes.
 *
 * Headless, as gencheck is: the scheduler's virtual clock, no audio.
 * EDITCHECK_DUMP=<dir> writes each piece's three tapes there, for reading a
 * failure.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm.h>

#include "think.h"

#include "libthink/thDynLib.h"
#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcGenEdit.h"
#include "thcGenDiff.h"

static int failures = 0;

static void
fail (const std::string &piece, const std::string &what)
{
    fprintf(stderr, "editcheck: FAIL: %s: %s\n", piece.c_str(),
            what.c_str());
    failures++;
}

/* The edits' times, and how long each run is, in transport seconds. */
static const double EDIT_A = 7.31;
static const double EDIT_B = 13.77;
static const double EDIT_C = 21.13;
static const double EDIT_D = 33.91;
static const double SECONDS = 60;

/* How long the added chain may go on being heard after it is taken away:
   what it had queued before then. */
static const double QUEUED_FOR = 20;

/* The channel the added chain plays on, engine numbering. */
static const int ADDED_CHANNEL = 15;

static std::string
slurp (const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;

    s << in.rdbuf();

    return s.str();
}

static void
spill (const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);

    out << text;
}

/* gencheck's, for gencheck's reasons: see there. */
static void
loadComposers (const std::string &pluginDir,
               std::map<std::string, thcPlugin *> &out)
{
    std::filesystem::path root =
        std::filesystem::path(pluginDir) / "composer";
    std::error_code ec;

    if (!std::filesystem::is_directory(root, ec))
        return;

    for (const auto &f : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() != PLUGIN_SUFFIX)
            continue;

        thcPlugin *p = new thcPlugin(f.path().string());

        if (p->state() != thcPlugin::LOADED)
        {
            delete p;
            continue;
        }

        out[p->name()] = p;
        thDynLib::open(f.path().string());
    }
}

/* One line per delivered event, genwav's spelling. */
static std::string
line (const thcEvent &ev)
{
    char buf[200];

    if (ev.type == THC_EV_NOTE)
        snprintf(buf, sizeof(buf), "N %.17g %d %d %d %.17g %.9g\n",
                 ev.at, ev.channel, ev.u.note.note, ev.u.note.velocity,
                 ev.u.note.duration, (double)ev.u.note.level);
    else if (ev.type == THC_EV_NOTEOFF)
        snprintf(buf, sizeof(buf), "F %.17g %d %d\n", ev.at, ev.channel,
                 ev.u.note.note);
    else if (ev.type == THC_EV_CHANARG)
        snprintf(buf, sizeof(buf), "C %.17g %d %s %.17g\n", ev.at,
                 ev.channel, ev.u.chanarg.name ? ev.u.chanarg.name : "",
                 (double)ev.u.chanarg.value);
    else if (ev.type == THC_EV_PATCH)
        snprintf(buf, sizeof(buf), "P %.17g %d %s\n", ev.at, ev.channel,
                 ev.u.patch.name ? ev.u.patch.name : "");
    else if (ev.type == THC_EV_NODEARG)
        snprintf(buf, sizeof(buf), "E %.17g %d %s %s %.17g\n", ev.at,
                 ev.channel, ev.u.nodearg.node ? ev.u.nodearg.node : "",
                 ev.u.nodearg.arg ? ev.u.nodearg.arg : "",
                 (double)ev.u.nodearg.value);
    else
        snprintf(buf, sizeof(buf), "? %.17g %d %d\n", ev.at, ev.channel,
                 (int)ev.type);

    return buf;
}

static int
channelOf (const std::string &l)
{
    int channel = -1;
    double at;
    char kind;

    sscanf(l.c_str(), "%c %lf %d", &kind, &at, &channel);

    return channel;
}

static double
timeOf (const std::string &l)
{
    int channel;
    double at = 0;
    char kind;

    sscanf(l.c_str(), "%c %lf %d", &kind, &at, &channel);

    return at;
}

static std::vector<std::string>
lines (const std::string &tape)
{
    std::vector<std::string> out;
    std::istringstream in(tape);
    std::string l;

    while (std::getline(in, l))
        out.push_back(l);

    return out;
}

/* The lines on the channels in `keep'. */
static std::string
onChannels (const std::string &tape, const std::set<int> &keep)
{
    std::string out;

    for (const std::string &l : lines(tape))
        if (keep.count(channelOf(l)))
            out += l + "\n";

    return out;
}

static std::string
firstDifference (const std::string &a, const std::string &b)
{
    std::vector<std::string> x = lines(a), y = lines(b);

    for (size_t i = 0; i < std::max(x.size(), y.size()); i++)
    {
        const std::string l = i < x.size() ? x[i] : "(end)";
        const std::string r = i < y.size() ? y[i] : "(end)";

        if (l != r)
            return "line " + std::to_string(i + 1) + ": '" + l +
                   "' against '" + r + "'";
    }

    return "none";
}

/* The harness's own types, in a namespace of their own: thcGenEdit.cpp
   has a `struct Edit' of its own at file scope, and two classes of one
   name in one program is one vector<Edit> destructor for both. */
namespace {

/* mulberry32, so a run is a run. */
struct Rng
{
    unsigned a;

    double operator() (void)
    {
        a += 0x6D2B79F5u;
        unsigned t = a;

        t = (t ^ (t >> 15)) * (t | 1);
        t ^= t + (t ^ (t >> 7)) * (t | 61);

        return ((t ^ (t >> 14)) >> 0) / 4294967296.0;
    }
};

/* One text at one transport time. */
struct Edit
{
    double      at;
    std::string text;
};

/* A peer: a synth, a scheduler on it, the text it is playing, and the tape
 * it delivered. The synth is its own because instruments are loaded onto
 * it, and two peers are two machines. */
struct Peer
{
    const std::map<std::string, thcPlugin *> &plugins;
    thSynth                                  synth;
    thcScheduler                             sched;
    std::filesystem::path                    cur, next;
    std::string                              tape;
    long                                     posted;
    sigc::connection                         conn;

    Peer (const std::map<std::string, thcPlugin *> &p,
          const std::string &pluginDir, const std::filesystem::path &dir,
          const std::string &name)
        : plugins(p), synth(pluginDir, TH_DEFAULT_WINDOW_LENGTH,
                            TH_DEFAULT_SAMPLES),
          sched(&synth), cur(dir / (name + ".gen")),
          next(dir / (name + "-next.gen")), posted(0)
    {
        conn = sched.sigDelivered.connect([this](const thcEvent &ev)
        {
            tape += line(ev);
            posted++;
        });
    }

    ~Peer (void)
    {
        conn.disconnect();
    }

    bool load (const std::string &text, std::vector<std::string> &errors)
    {
        thcGenLoader loader(plugins);

        spill(cur, text);

        bool ok = loader.load(cur.string(), &sched);

        errors = loader.errors();
        drain();

        return ok;
    }

    bool edit (const std::string &text, std::vector<std::string> &errors)
    {
        spill(next, text);

        if (!thcGenDiff::apply(sched, plugins, cur.string(), next.string(),
                               std::set<std::string>(), errors) ||
            !errors.empty())
            return false;

        spill(cur, text);
        drain();

        return true;
    }

    /* The command ring, emptied as an audio thread would: see gencheck's
       render for the cadence. */
    void drain (void)
    {
        synth.process();
        posted = 0;
    }

    void stepTo (double t)
    {
        sched.stepTransportTo(t);

        if (posted >= TH_COMMAND_QUEUE_SIZE / 4)
            drain();
    }
};

} /* namespace */

/* A run of `seconds', with `edits' applied at their times. `step' says how
 * far the next step goes from where the transport is. Returns false, having
 * failed, when an edit did not load.
 */
static bool
play (Peer &p, const std::string &piece, const std::vector<Edit> &edits,
      double seconds, const std::function<double (void)> &step,
      const std::function<void (Peer &, double)> &at = nullptr)
{
    size_t e = 0;

    p.sched.start();

    while (p.sched.now() < seconds && p.sched.running())
    {
        double t = std::min(seconds, p.sched.now() + step());

        if (e < edits.size() && edits[e].at <= t)
            t = edits[e].at;

        p.stepTo(t);

        if (at)
            at(p, t);

        while (e < edits.size() && edits[e].at <= p.sched.now())
        {
            std::vector<std::string> errors;

            if (!p.edit(edits[e].text, errors))
            {
                fail(piece, "the edit at " + std::to_string(edits[e].at) +
                     " did not apply: " +
                     (errors.empty() ? "" : errors[0]));
                return false;
            }

            e++;
        }
    }

    p.sched.stop();
    p.drain();

    return true;
}

/* A scratch copy of `text' with `op' done to it; empty if the op refused. */
static std::string
edited (const std::filesystem::path &scratch, const std::string &text,
        const std::function<thcGenEdit::Result (const std::string &,
                                                std::string &)> &op)
{
    std::string why;

    spill(scratch, text);

    if (op(scratch.string(), why) != thcGenEdit::OK)
        return std::string();

    return slurp(scratch);
}

/* Does `text' load? On a scheduler of its own and a synth of its own, so
   nothing a peer holds is touched. */
static bool
loads (const std::map<std::string, thcPlugin *> &plugins,
       const std::string &pluginDir, const std::filesystem::path &scratch,
       const std::string &text)
{
    thSynth synth(pluginDir, TH_DEFAULT_WINDOW_LENGTH, TH_DEFAULT_SAMPLES);
    thcScheduler sched(&synth);
    thcGenLoader loader(plugins);

    spill(scratch, text);

    bool ok = loader.load(scratch.string(), &sched);

    synth.process();

    return ok;
}

/* The numeric param of a composer stage in `chain' that edit B changes:
   its first one whose text is a plain number and which still loads when
   nudged. False if the chain has none. */
static bool
nudge (const std::map<std::string, thcPlugin *> &plugins,
       const std::string &pluginDir, const std::filesystem::path &scratch,
       const std::string &text, const thcGenEdit::Chain &chain,
       std::string &out)
{
    for (size_t si = 0; si < chain.stages.size(); si++)
    {
        const thcGenEdit::Stage &st = chain.stages[si];

        if (thcGenEdit::isNodeStage(st))
            continue;

        for (const thcGenEdit::Param &pa : st.params)
        {
            char *end = NULL;
            const double v = strtod(pa.valueText.c_str(), &end);

            if (pa.valueText.empty() || end == NULL || *end != '\0' ||
                v == 0)
                continue;

            std::string to;

            if (!thcGenEdit::format(v == std::floor(v) ? v + 1 : v * 0.9,
                                    to))
                continue;

            std::string next = edited(scratch, text,
                [&](const std::string &f, std::string &why)
                {
                    return thcGenEdit::setParam(f, chain.name, (int)si,
                                                pa.name, to, why);
                });

            if (!next.empty() && next != text &&
                loads(plugins, pluginDir, scratch, next))
            {
                out = next;
                return true;
            }
        }
    }

    return false;
}

static void
checkPiece (const std::map<std::string, thcPlugin *> &plugins,
            const std::string &pluginDir, const std::filesystem::path &dir,
            const std::filesystem::path &path)
{
    const std::string piece = path.filename().string();
    const std::string text = slurp(path);
    const std::filesystem::path scratch = dir / "scratch.gen";
    thcGenEdit::Doc doc;
    std::string why;

    spill(scratch, text);

    if (thcGenEdit::describe(scratch.string(), doc, why) != thcGenEdit::OK)
    {
        fail(piece, "does not describe: " + why);
        return;
    }

    if (!doc.hasSeed || doc.chains.empty())
        return;

    /* The edits, each on top of the one before. */
    const std::string a = "# an edit that changes no stage\n" + text;

    std::string b;
    size_t ei = doc.chains.size();

    while (ei > 0 &&
           !nudge(plugins, pluginDir, scratch, a, doc.chains[ei - 1], b))
        ei--;

    if (ei == 0)
    {
        printf("skip  %-16s no chain has a param to nudge\n", piece.c_str());
        return;
    }

    const thcGenEdit::Chain &edChain = doc.chains[ei - 1];

    /* The added chain: a copy of the first generator the piece has, as its
       own chain on a channel nothing else plays on. */
    const thcGenEdit::Stage *gen = NULL;

    for (const thcGenEdit::Chain &ch : doc.chains)
        for (const thcGenEdit::Stage &st : ch.stages)
            if (gen == NULL && st.category == "gen")
                gen = &st;

    std::string c;

    if (gen != NULL)
    {
        std::vector<std::pair<std::string, std::string> > params;

        /* Not a param that reads a node: the node is its old chain's, and
           the copy is a chain of its own. It plays that param's default. */
        for (const thcGenEdit::Param &pa : gen->params)
            if (pa.valueText.find("->") == std::string::npos)
                params.push_back(std::make_pair(pa.name, pa.valueText));

        c = edited(scratch, b, [&](const std::string &f, std::string &w)
        {
            return thcGenEdit::addChain(f, "added", ADDED_CHANNEL + 1, "",
                                        "g", "gen", gen->plugin, params, w);
        });

        if (!c.empty() && !loads(plugins, pluginDir, scratch, c))
            c.clear();
    }

    if (c.empty())
    {
        printf("skip  %-16s it has no generator to copy\n", piece.c_str());
        return;
    }

    const std::string d = edited(scratch, c,
        [&](const std::string &f, std::string &w)
        {
            return thcGenEdit::removeChain(f, "added", w);
        });

    const std::vector<Edit> edits = {
        { EDIT_A, a }, { EDIT_B, b }, { EDIT_C, c }, { EDIT_D, d },
    };

    std::vector<std::string> errors;

    /* The run nobody edited. */
    Peer base(plugins, pluginDir, dir, "base");

    if (!base.load(text, errors))
    {
        fail(piece, "does not load: " + (errors.empty() ? "" : errors[0]));
        return;
    }

    play(base, piece, {}, SECONDS, [] { return 1024.0 / 44100; });

    /* And nobody edited it with the other peer's steps: whether the piece
       is one tape at the two step sizes before any edit is involved. */
    {
        Peer q0(plugins, pluginDir, dir, "q0");
        Rng r0 = { 0x5eed };

        q0.load(text, errors);
        play(q0, piece, {}, SECONDS, [&r0] { return 0.002 + r0() * 0.058; });

        if (q0.tape != base.tape)
            fail(piece, "differs between the two step sizes with no edit: " +
                 firstDifference(base.tape, q0.tape));
    }

    /* The two peers. */
    Peer P(plugins, pluginDir, dir, "p");
    Peer Q(plugins, pluginDir, dir, "q");
    Rng rng = { 0x5eed };

    P.load(text, errors);
    Q.load(text, errors);

    if (!play(P, piece, edits, SECONDS, [] { return 1024.0 / 44100; }) ||
        !play(Q, piece, edits, SECONDS,
              [&rng] { return 0.002 + rng() * 0.058; }))
        return;

    if (getenv("EDITCHECK_DUMP") != NULL)
    {
        const std::filesystem::path out = getenv("EDITCHECK_DUMP");

        spill(out / (piece + ".base"), base.tape);
        spill(out / (piece + ".p"), P.tape);
        spill(out / (piece + ".q"), Q.tape);
    }

    /* 1. One tape. */
    if (P.tape != Q.tape)
        fail(piece, "the two peers differ: " +
             firstDifference(P.tape, Q.tape));

    /* 2. The channels only unedited chains play on. Read off the base
       run's scheduler, where every sink's channel is a number. */
    std::set<int> editedChannels, others;

    for (size_t i = 0; i < base.sched.chainCount(); i++)
    {
        const thcChain *ch = base.sched.chain(i);

        for (const thcSink &s : ch->sinks)
            (ch->name == edChain.name ? editedChannels : others)
                .insert(s.channel);
    }

    for (int ch : editedChannels)
        others.erase(ch);

    others.erase(ADDED_CHANNEL);

    const std::string kept = onChannels(P.tape, others);
    const std::string want = onChannels(base.tape, others);

    if (kept != want)
        fail(piece, "a chain no edit touched played differently: " +
             firstDifference(want, kept));

    /* And the edit did something: from B on, the edited chain's channels
       are not what the base run delivered -- unless the param is one the
       piece does not hear, which is the piece's business. */
    const bool moved = onChannels(P.tape, editedChannels) !=
                       onChannels(base.tape, editedChannels);

    /* 3. The added chain. */
    size_t before = 0, during = 0, after = 0;

    for (const std::string &l : lines(P.tape))
    {
        if (channelOf(l) != ADDED_CHANNEL)
            continue;

        const double t = timeOf(l);

        if (t < EDIT_C)
            before++;
        else if (t < EDIT_D + QUEUED_FOR)
            during++;
        else
            after++;
    }

    if (before != 0 || after != 0)
        fail(piece, "the added chain was heard " + std::to_string(before) +
             " times before it was added and " + std::to_string(after) +
             " times after it was taken away");

    /* Heard, if it is heard at all: loaded from the top, over as long as
       it was in the edited run, does the chain make a sound? A copy of a
       generator that waits for input, or whose first note is further off
       than that, is silent in both. */
    if (during == 0)
    {
        Peer alone(plugins, pluginDir, dir, "alone");

        alone.load(c, errors);
        play(alone, piece, {}, EDIT_D - EDIT_C, [] { return 1024.0 / 44100; });

        if (!onChannels(alone.tape, { ADDED_CHANNEL }).empty())
            fail(piece, "the added chain was never heard, and it is when "
                 "the text is loaded from the top");
    }

    /* 4. A rewind is a load of the final text. */
    Peer fresh(plugins, pluginDir, dir, "fresh");

    fresh.load(d, errors);
    play(fresh, piece, {}, 20, [] { return 1024.0 / 44100; });

    P.sched.reset();
    P.tape.clear();
    play(P, piece, {}, 20, [] { return 1024.0 / 44100; });

    if (P.tape != fresh.tape)
        fail(piece, "a rewind after the edits is not a load of the text: " +
             firstDifference(fresh.tape, P.tape));

    printf("ok    %-16s %5zu events; %zu channels kept through 4 edits, "
           "%s edited%s; the added chain %zu events\n",
           piece.c_str(), lines(Q.tape).size(), others.size(),
           edChain.name.c_str(), moved ? "" : " (unheard)", during);
}

/* The synth carries the tempo misc::tempo reads: the piece's on start,
   an edit's new `tempo' line once adopted, and 120 again for a piece
   with no line loaded into the scheduler that played one. */
static void
checkTempo (const std::map<std::string, thcPlugin *> &plugins,
            const std::string &pluginDir, const std::filesystem::path &dir,
            const std::filesystem::path &tempoed,
            const std::filesystem::path &untempoed)
{
    const std::string piece = tempoed.filename().string();
    const std::string text = slurp(tempoed);
    const size_t at = text.find("\ntempo ");
    std::vector<std::string> errors;
    Peer p(plugins, pluginDir, dir, "t");

    if (at == std::string::npos || !p.load(text, errors))
    {
        fail(piece, "wanted a piece with a tempo line that loads");
        return;
    }

    const float bpm = (float)p.sched.tempo();

    p.sched.start();
    p.stepTo(1);
    if (p.synth.tempo() != bpm)
        fail(piece, "the synth's tempo is " +
             std::to_string(p.synth.tempo()) + ", not the piece's " +
             std::to_string(bpm));

    const size_t eol = text.find(';', at);
    const std::string next =
        text.substr(0, at) + "\ntempo 137" + text.substr(eol);

    if (!p.edit(next, errors) || p.synth.tempo() != 137)
        fail(piece, "an edit to tempo 137 left the synth at " +
             std::to_string(p.synth.tempo()));

    p.sched.stop();
    if (!p.load(slurp(untempoed), errors))
    {
        fail(untempoed.filename().string(), "does not load");
        return;
    }
    if (p.synth.tempo() != 120)
        fail(untempoed.filename().string(),
             "with no tempo line, loaded after a piece at 137, left the "
             "synth at " + std::to_string(p.synth.tempo()));
}

/* 5 and 6: knobs, and a text that does not load. */
static void
checkKnobsAndRefusal (const std::map<std::string, thcPlugin *> &plugins,
                      const std::string &pluginDir,
                      const std::filesystem::path &dir,
                      const std::filesystem::path &path)
{
    const std::string piece = path.filename().string();
    const std::string text = slurp(path);
    const std::filesystem::path scratch = dir / "scratch.gen";
    thcGenEdit::Doc doc;
    std::string why;

    spill(scratch, text);

    if (thcGenEdit::describe(scratch.string(), doc, why) != thcGenEdit::OK ||
        doc.knobs.size() < 2)
    {
        fail(piece, "wanted a piece with two knobs for the knob check");
        return;
    }

    const thcGenEdit::Knob &k0 = doc.knobs[0], &k1 = doc.knobs[1];
    const double moved0 = k0.hasMin && k0.hasMax
                              ? k0.min + (k0.max - k0.min) * 0.37
                              : k0.value + 0.123;
    const double moved1 = k1.hasMin && k1.hasMax
                              ? k1.min + (k1.max - k1.min) * 0.61
                              : k1.value + 0.321;
    std::string to;

    thcGenEdit::format(k1.hasMin && k1.hasMax
                           ? k1.min + (k1.max - k1.min) * 0.2
                           : k1.value + 1, to);

    const double declared1 = atof(to.c_str());
    const std::string next = edited(scratch, text,
        [&](const std::string &f, std::string &w)
        {
            return thcGenEdit::setKnobValue(f, k1.name, declared1, w);
        });

    std::vector<std::string> errors;
    Peer p(plugins, pluginDir, dir, "k");

    p.load(text, errors);
    p.sched.start();
    p.stepTo(2);
    p.sched.knob(k0.name)->setValue((float)moved0);
    p.sched.knob(k1.name)->setValue((float)moved1);
    p.stepTo(3);

    /* 6, the knobs: a text that does not load -- whose declaration of
       @k1 is not the one playing -- leaves both knobs where they were. */
    if (p.edit(next + "\nchain broken { stage x gen::no_such_plugin { }; };\n",
               errors))
        fail(piece, "a text that does not load was applied");

    if ((*p.sched.knob(k0.name))[0] != (float)moved0 ||
        (*p.sched.knob(k1.name))[0] != (float)moved1)
        fail(piece, "a text that did not load moved a knob: @" + k0.name +
             " " + std::to_string((*p.sched.knob(k0.name))[0]) + ", @" +
             k1.name + " " + std::to_string((*p.sched.knob(k1.name))[0]));

    if (!p.edit(next, errors))
    {
        fail(piece, "the knob edit did not apply: " +
             (errors.empty() ? "" : errors[0]));
        return;
    }

    const float v0 = (*p.sched.knob(k0.name))[0];
    const float v1 = (*p.sched.knob(k1.name))[0];

    if (v0 != (float)moved0)
        fail(piece, "@" + k0.name + " was moved to " +
             std::to_string(moved0) + " and an edit that did not touch it "
             "left it at " + std::to_string(v0));

    if (v1 != (float)declared1)
        fail(piece, "@" + k1.name + "'s declaration was changed to " +
             std::to_string(declared1) + " and it is " + std::to_string(v1));

    /* 6. A text that does not load. */
    const std::string before = p.tape;
    const size_t chains = p.sched.chainCount();

    if (p.edit(text + "\nchain broken { stage x gen::no_such_plugin { }; };\n",
               errors))
        fail(piece, "a text that does not load was applied");
    else if (errors.empty())
        fail(piece, "a text that does not load was refused without a word");

    if (p.sched.chainCount() != chains || !p.sched.running())
        fail(piece, "a text that did not load changed the piece");

    p.stepTo(10);

    if (p.tape.size() <= before.size())
        fail(piece, "the piece stopped after a refused edit");

    p.sched.stop();
    p.drain();

    printf("ok    %-16s a moved knob kept, a changed declaration taken, a "
           "text that does not load refused (%s)\n", piece.c_str(),
           errors.empty() ? "" : errors[0].c_str());
}

/* A device that answers to every name and remembers which channel each
   attach asked for. */
struct StubMidiOut : public thcMidiOut
{
    std::vector<int> channels;

    bool attach (int, const thcInstrument &inst, std::string &) override
    {
        channels.push_back(inst.midiChannel);
        return true;
    }

    void detach (int) override {}
    void noteOn (int, int, int, float, gint64) override {}
    void noteOff (int, int, gint64) override {}
    void control (int, const std::string &, double, gint64) override {}
    void flush (int) override {}
};

static std::string
smallChain (const std::string &name, int channel, const std::string &extra)
{
    return "chain " + name + " {\n" + extra +
           "    stage src gen::euclid { steps = 4; fills = 4; rotate = 0; "
           "notes = \"C3\"; period = 1 beats; hold = 0.25 beats; "
           "vel = 100; };\n"
           "    sink { channel = " + std::to_string(channel) + "; };\n};\n";
}

static std::string
knobMeta (thArg *k)
{
    char buf[100];

    snprintf(buf, sizeof(buf), "min %g max %g step %g ", k->min(), k->max(),
             k->step());

    return buf + ("label '" + k->label() + "' units '" + k->units() + "'");
}

/* What one edit to a small text has to do. `ready' runs after the load and
   before the start; `check' after the edit, and says what is wrong. */
namespace {

struct Case
{
    const char *what;
    std::string before, after;
    double at;
    std::function<void (Peer &)> ready;
    std::function<std::string (Peer &)> check;
};

} /* namespace */

static void
checkCases (const std::map<std::string, thcPlugin *> &plugins,
            const std::string &pluginDir, const std::filesystem::path &dir)
{
    const std::string head = "tempo 120;\n@k = 5;\n@k.max = 16;\n";
    const std::string late = smallChain("x", 1, "") +
                             smallChain("late", 2, "    start = 32 beats;\n");
    const std::string broken =
        "chain broken { stage s gen::no_such_plugin { }; };\n";
    auto device = [](int channel)
    {
        return "tempo 120;\ninstrument lead { midi \"Dev\"; midichannel = " +
               std::to_string(channel) + "; };\n"
               "chain x { stage src gen::euclid { steps = 4; fills = 4; "
               "rotate = 0; notes = \"C3\"; period = 1 beats; "
               "hold = 0.25 beats; vel = 100; }; "
               "sink { instrument = lead; }; };\n";
    };
    auto shifted = [](int semitones)
    {
        return "chain keys {\n    input midi;\n"
               "    stage t xform::transpose { semitones = " +
               std::to_string(semitones) + "; };\n"
               "    sink { channel = 1; };\n};\n";
    };
    const std::string writer =
        "chain m {\n    stage w gen::walk { min = 0.6; max = 1.4; "
        "step = 0.2; period = 0.5 s; };\n    sink { knob = @w; };\n};\n";
    auto homeIs = [](float want)
    {
        return [want](Peer &p)
        {
            p.sched.reset();

            const float got = (*p.sched.knob("w"))[0];

            return got == want ? std::string()
                               : "a rewind put @w at " +
                                     std::to_string(got);
        };
    };
    StubMidiOut out;
    std::vector<std::string> errors;

    /* The knob as a fresh load of `text' has it. */
    auto freshMeta = [&](const std::string &text)
    {
        Peer f(plugins, pluginDir, dir, "fresh");

        f.load(text, errors);

        return knobMeta(f.sched.knob("k"));
    };

    auto metaAsLoaded = [&](const std::string &text)
    {
        return [&, text](Peer &p)
        {
            const std::string got = knobMeta(p.sched.knob("k")),
                              want = freshMeta(text);

            return got == want ? std::string()
                               : "@k is " + got + ", and " + want +
                                 " loaded";
        };
    };

    auto press = [](Peer &p, thcEventType type)
    {
        thcEvent ev = {};

        ev.type = type;
        ev.at = p.sched.now();
        ev.channel = 0;
        ev.u.note.note = 60;
        ev.u.note.velocity = type == THC_EV_NOTE ? 100 : 0;
        p.sched.injectMidi(0, ev);
    };

    const std::vector<Case> cases = {
        { "a chain not started waits for its edited start",
          head + late, head + smallChain("x", 1, "") +
          smallChain("late", 2, "    start = 8 beats;\n"), 1.01, nullptr,
          [](Peer &p)
          {
              p.stepTo(10);

              for (const std::string &l : lines(p.tape))
                  if (l[0] == 'N' && channelOf(l) == 1)
                      return fabs(timeOf(l) - 4) < 1e-9
                                 ? std::string()
                                 : "its first note is at " +
                                       std::to_string(timeOf(l)) +
                                       " s, not 4";

              return std::string("it never played");
          } },
        { "a chain keeps what it was heard to play", head + late,
          "# a comment\n" + head + late, 6.01, nullptr,
          [](Peer &p)
          {
              const thcChain *c = p.sched.chain(0);

              return c->played.size() >= 10 && c->lastHeard[0] > 5
                         ? std::string()
                         : std::to_string(c->played.size()) +
                               " notes played, last heard at " +
                               std::to_string(c->lastHeard[0]);
          } },
        { "a text that does not load leaves a knob's metadata",
          head + late,
          "tempo 120;\n@k = 5;\n@k.max = 99;\n@k.label = \"Changed\";\n" +
          late + broken, 1.01, nullptr, metaAsLoaded(head + late) },
        { "an edit gives a knob the new text's metadata, and only that",
          head + late,
          "tempo 120;\n@k = 5;\n@k.label = \"Changed\";\n" + late, 1.01,
          nullptr,
          metaAsLoaded("tempo 120;\n@k = 5;\n@k.label = \"Changed\";\n" +
                       late) },
        { "a device's channel changed reaches the device",
          device(3), device(9), 1.01,
          [&](Peer &p) { p.sched.setMidiOut(&out); },
          [&](Peer &)
          {
              return !out.channels.empty() && out.channels.back() == 8
                         ? std::string()
                         : "the device was last attached on " +
                               std::to_string(out.channels.empty()
                                                  ? -1
                                                  : out.channels.back());
          } },
        { "what a removed chain queued is no other chain's",
          head + "chain gone {\n"
          "    stage src gen::euclid { steps = 4; fills = 4; rotate = 0; "
          "notes = \"C3\"; period = 1 beats; hold = 0.25 beats; "
          "vel = 100; };\n"
          "    stage e xform::echo { repeats = 4; time = 2 s; pass = 1; };\n"
          "    sink { channel = 1; };\n};\n" +
          smallChain("late", 2, "    start = 30 s;\n"),
          head + smallChain("late", 2, "    start = 30 s;\n"), 1.01, nullptr,
          [](Peer &p)
          {
              p.stepTo(9);

              const thcChain *c = p.sched.chain(0);

              return c->played.empty() && c->lastHeard[0] < 0
                         ? std::string()
                         : "the chain that has not started was heard " +
                               std::to_string(c->played.size()) + " times";
          } },
        { "a key held through a replaced stage is released",
          head + shifted(2), head + shifted(5), 1.01,
          [&](Peer &p) { press(p, THC_EV_NOTE); },
          [&](Peer &p)
          {
              press(p, THC_EV_NOTEOFF);
              p.stepTo(2);

              std::multiset<std::pair<int, int> > held;

              for (const std::string &l : lines(p.tape))
              {
                  double at;
                  int ch, note;

                  if (sscanf(l.c_str() + 1, "%lf %d %d", &at, &ch, &note) != 3)
                      continue;

                  if (l[0] == 'N')
                      held.insert({ ch, note });
                  else if (l[0] == 'F' && held.count({ ch, note }))
                      held.erase(held.find({ ch, note }));
              }

              return held.empty()
                         ? std::string()
                         : "note " + std::to_string(held.begin()->second) +
                               " was never released";
          } },
        { "a rewind puts a written knob at its edited declaration",
          head + "@w = 0.25;\n" + writer, head + "@w = 0.5;\n" + writer,
          1.01, nullptr, homeIs(0.5f) },
        { "a rewind puts a written knob at its declaration, not at Play's",
          head + "@w = 0.25;\n" + writer,
          "# a comment\n" + head + "@w = 0.25;\n" + writer, 1.01,
          [](Peer &p) { p.sched.knob("w")->setValue(0.9f); }, homeIs(0.25f) },
        { "a changed tempo line changes the tempo", head + late,
          "tempo 90;\n" + head.substr(head.find('\n') + 1) + late, 1.01,
          nullptr,
          [](Peer &p)
          {
              return p.sched.tempo() == 90
                         ? std::string()
                         : "the tempo is " + std::to_string(p.sched.tempo());
          } },
        { "an unchanged tempo line leaves a moved tempo", head + late,
          "# a comment\n" + head + late, 1.01,
          [](Peer &p) { p.sched.setTempo(100); },
          [](Peer &p)
          {
              return p.sched.tempo() == 100
                         ? std::string()
                         : "the tempo is " + std::to_string(p.sched.tempo());
          } },
    };

    for (const Case &c : cases)
    {
        Peer p(plugins, pluginDir, dir, "case");

        if (!p.load(c.before, errors))
        {
            fail(c.what, "did not load: " +
                 (errors.empty() ? "" : errors[0]));
            continue;
        }

        if (c.ready)
            c.ready(p);

        p.sched.start();

        for (double t = 0.02; t < c.at; t += 0.02)
            p.stepTo(t);

        p.stepTo(c.at);

        p.edit(c.after, errors);

        const std::string wrong = c.check(p);

        p.sched.stop();
        p.drain();

        if (wrong.empty())
            printf("ok    %s\n", c.what);
        else
            fail(c.what, wrong);
    }
}

int
main (int argc, char *argv[])
{
    Glib::init();

    std::string pluginDir, genDir;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            pluginDir = argv[++i];
        else
            genDir = argv[i];
    }

    if (pluginDir.empty() || genDir.empty())
    {
        fprintf(stderr, "usage: editcheck -p <plugindir> <gendir>\n");
        return 2;
    }

    std::map<std::string, thcPlugin *> plugins;

    loadComposers(pluginDir, plugins);

    if (plugins.empty())
    {
        fprintf(stderr, "editcheck: no composer modules in %s\n",
                pluginDir.c_str());
        return 2;
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("editcheck-" + std::to_string(getpid()));

    std::filesystem::create_directories(dir);

    std::vector<std::filesystem::path> pieces;

    for (const auto &e : std::filesystem::directory_iterator(genDir))
        if (e.path().extension() == ".gen")
            pieces.push_back(e.path());

    std::sort(pieces.begin(), pieces.end());

    for (const std::filesystem::path &p : pieces)
        checkPiece(plugins, pluginDir, dir, p);

    checkKnobsAndRefusal(plugins, pluginDir, dir,
                         std::filesystem::path(genDir) / "orrery.gen");
    checkTempo(plugins, pluginDir, dir,
               std::filesystem::path(genDir) / "orrery.gen",
               std::filesystem::path(genDir) / "airports.gen");
    checkCases(plugins, pluginDir, dir);

    std::error_code ec;

    std::filesystem::remove_all(dir, ec);

    for (auto &p : plugins)
        delete p.second;

    if (failures == 0)
        printf("editcheck: OK\n");
    else
        printf("editcheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
