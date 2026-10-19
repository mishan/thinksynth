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
 *      with no edits at all: a chain whose text did not change kept its
 *      instances, their wakes and their state, whatever happened around
 *      it. And from B on, the edited chain's channels play what B's text
 *      loaded from the top plays, as the added chain's does from C on:
 *      a chain an edit builds comes in where it would have been.
 *   3. The added chain is not heard before its edit, and is silent once
 *      it is taken away.
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

/* The notes on the channels in `keep' struck in [from, to). Not the offs:
   one for a note struck before `from' is a different note in each run. */
static std::string
notesBetween (const std::string &tape, const std::set<int> &keep,
              double from, double to)
{
    std::string out;

    for (const std::string &l : lines(tape))
        if (l[0] == 'N' && keep.count(channelOf(l)) && timeOf(l) >= from &&
            timeOf(l) < to)
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
        else if (t < EDIT_D)
            during++;
        else if (l[0] == 'N')
            after++;
    }

    if (before != 0 || after != 0)
        fail(piece, "the added chain was heard " + std::to_string(before) +
             " times before it was added and " + std::to_string(after) +
             " times after it was taken away");

    /* 2, the chains the edits built, against their texts loaded from the
       top. Not an edited chain with nodes: an edit leaves those where
       they were rather than playing them up to it. */
    auto fromTop = [&](const std::string &t, double seconds)
    {
        Peer top(plugins, pluginDir, dir, "top");

        top.load(t, errors);
        play(top, piece, {}, seconds, [] { return 1024.0 / 44100; });

        return top.tape;
    };

    bool nodes = false;

    for (size_t i = 0; i < base.sched.chainCount(); i++)
        if (base.sched.chain(i)->name == edChain.name)
            nodes = base.sched.chain(i)->nodes != nullptr;

    if (!nodes)
    {
        const std::string top = fromTop(b, EDIT_C);
        const std::string want = notesBetween(top, editedChannels, EDIT_B,
                                              EDIT_C);
        const std::string got = notesBetween(P.tape, editedChannels, EDIT_B,
                                             EDIT_C);

        if (got != want)
            fail(piece, "from its edit on, the edited chain did not play "
                 "what its text plays from the top: " +
                 firstDifference(want, got));
    }

    {
        const std::string top = fromTop(c, EDIT_D);
        const std::string want = notesBetween(top, { ADDED_CHANNEL },
                                              EDIT_C, EDIT_D);
        const std::string got = notesBetween(P.tape, { ADDED_CHANNEL },
                                             EDIT_C, EDIT_D);

        if (got != want)
            fail(piece, "from its edit on, the added chain did not play "
                 "what its text plays from the top: " +
                 firstDifference(want, got));
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

/* A param poked into a chain that composes ahead, and the chain composed
   again (thcScheduler::recompose): from then on it plays what a text with
   the new value plays from the top, not the rest of a phrase composed with
   the old one. */
static void
checkRecompose (const std::map<std::string, thcPlugin *> &plugins,
                const std::string &pluginDir, const std::filesystem::path &dir)
{
    const double at = 5.3, until = 12;
    auto lfo = [](const std::string &freq)
    {
        return "    stage lfo osc::simple { freq = " + freq +
               "; waveform = 0; amp = 1; };\n"
               "    stage half math::mul { in0 = lfo->out; in1 = 0.5; };\n"
               "    stage mid math::add { in0 = half->out; in1 = 0.5; };\n";
    };

    /* No nodes; an LFO; and an LFO whose rate is a knob moved after a
       phrase was composed and before the param is set: the phrase played
       again reads the LFO at the new rate, as a play from the top does,
       and not as it was. */
    for (const int nodes : { 0, 1, 2 })
    {
        auto text = [&](int vel, double rate)
        {
            return "tempo 120;\n@r = " + std::to_string(rate) +
                   ";\nchain a {\n" +
                   (nodes == 2 ? "    start = 1 s;\n" : "") +
                   (nodes == 0 ? "" : lfo(nodes == 1 ? "0.3" : "@r")) +
                   "    stage src gen::lsystem { axiom = " +
                   (nodes == 2 ? "\"F_F_F_F_F_F_F_F_F_F_F_F_\""
                               : "\"FFFFFFFFFFFFFFFF\"") +
                   "; depth = 0; notes = \"C4 D4 "
                   "E4\"; step = 0.5 beats; hold = 0.25 beats; vel = " +
                   std::to_string(vel) + "; };\n" +
                   (nodes == 0 ? ""
                               : "    stage l xform::level { "
                                 "gain = mid->out; };\n") +
                   "    sink { channel = 1; };\n};\n";
        };
        const std::string what = nodes == 0 ? ""
                               : nodes == 1 ? ", through an LFO"
                               : ", through an LFO whose rate was moved";
        std::vector<std::string> errors;
        Peer p(plugins, pluginDir, dir, "r"),
             top(plugins, pluginDir, dir, "rt");

        p.load(text(100, 0.3), errors);
        top.load(text(50, 0.7), errors);
        play(top, "recompose", {}, until, [] { return 1024.0 / 44100; });
        play(p, "recompose", {}, 3, [] { return 1024.0 / 44100; });
        p.sched.knob("r")->setValue(0.7f);
        play(p, "recompose", {}, at, [] { return 1024.0 / 44100; });

        p.sched.chain(0)->stages[0]->params.set("vel", 50);
        p.sched.recompose(0);
        play(p, "recompose", {}, until, [] { return 1024.0 / 44100; });

        const std::string want = notesBetween(top.tape, { 0 }, at, until);
        const std::string got = notesBetween(p.tape, { 0 }, at, until);

        if (want.empty() || got != want)
            fail("recompose", "a param poked into a phrase" + what +
                 " is not heard from then on: " +
                 firstDifference(want, got));
        else
            printf("ok    a param poked into a phrase%s is heard from then "
                   "on\n", what.c_str());
    }
}

/* A knob moved under a chain that composes ahead, by a hand or by an edit
   that declares it anew, is heard from then on, as the knob's new value
   played from the top is. */
static void
checkKnobMoved (const std::map<std::string, thcPlugin *> &plugins,
                const std::string &pluginDir, const std::filesystem::path &dir)
{
    const double at = 5.3, until = 12;
    std::vector<std::string> errors;

    /* Read straight, and through arithmetic, which is a node. */
    for (const std::string vel : { "@v", "@v / 2 + 25" })
    for (const bool byEdit : { false, true })
    {
        auto text = [&vel](int v)
        {
            return "tempo 120;\n@v = " + std::to_string(v) +
                   ";\n@v.max = 127;\nchain a {\n    stage src gen::lsystem "
                   "{ axiom = \"FFFFFFFFFFFFFFFF\"; depth = 0; notes = "
                   "\"C4 D4 E4\"; step = 0.5 beats; hold = 0.25 beats; "
                   "vel = " + vel + "; };\n    sink { channel = 1; };\n};\n";
        };
        const std::string how = std::string(byEdit ? "an edit" : "a hand") +
                                ", read as " + vel;
        Peer p(plugins, pluginDir, dir, "k"),
             top(plugins, pluginDir, dir, "kt");

        p.load(text(100), errors);
        top.load(text(50), errors);
        play(top, "knob moved", {}, until, [] { return 1024.0 / 44100; });
        play(p, "knob moved", {}, at, [] { return 1024.0 / 44100; });

        if (byEdit)
            p.edit(text(50), errors);
        else
        {
            p.sched.knob("v")->setValue(50);
            p.sched.knobMoved(p.sched.knob("v"), false);
        }

        play(p, "knob moved", {}, until, [] { return 1024.0 / 44100; });

        const std::string want = notesBetween(top.tape, { 0 }, at, until);
        const std::string got = notesBetween(p.tape, { 0 }, at, until);

        if (want.empty() || got != want)
            fail("knob moved", "a knob moved by " + how + " under a phrase "
                 "is not heard from then on: " + firstDifference(want, got));
        else
            printf("ok    a knob moved by %s under a phrase is heard from "
                   "then on\n", how.c_str());
    }
}

/* Deferred replays (setReplayDeferred), stepped as the browser's worklet
 * steps them: at the top of each window, a few ticks on, landed in the
 * window that finds them caught up. `chain a' reads @v and @h; `chain b'
 * reads neither. */
static std::string
replayPiece (int v)
{
    return "tempo 120;\n@v = " + std::to_string(v) + ";\n@v.max = 127;\n"
           "@h = 0.0125;\n"
           "chain a {\n    stage src gen::lsystem { axiom = "
           "\"FFFFFFFFFFFFFFFF\"; depth = 0; notes = \"C4 D4 E4 G4 A4\"; "
           "step = 0.05 beats; hold = @h; vel = @v; };\n"
           "    sink { channel = 1; };\n};\n"
           "chain b {\n    stage src gen::lsystem { axiom = \"FFFF\"; "
           "depth = 0; notes = \"C3 G3\"; step = 0.5 beats; "
           "hold = 0.25 beats; vel = 90; };\n    sink { channel = 2; };\n};\n";
}

/* `p' from now to `until', its replays advanced `ticks' a window and landed
   in the first window that finds them caught up -- or, with `landAt', in
   the first window starting at or after it, however early they caught up.
   `between' runs at the top of each window. Where they landed. */
static double
playReplaying (Peer &p, double until, unsigned ticks, double landAt = -1,
               const std::function<void (Peer &)> &between = nullptr)
{
    const double window = 1024.0 / 44100;
    double landed = -1;

    p.sched.start();

    while (p.sched.now() < until)
    {
        const double from = p.sched.now();

        if (between)
            between(p);

        if (p.sched.replaying() && p.sched.replayAdvance(ticks) &&
            (landAt < 0 || from >= landAt))
        {
            p.sched.replayLand();
            landed = from;
        }

        p.stepTo(std::min(until, from + window));
    }

    p.sched.stop();
    p.drain();

    return landed;
}

/* The chain the knob moved under plays what it had queued until its replay
   lands, and from there what it would have had the knob been there from the
   top; the chain beside it plays on as if nothing had happened. */
static void
checkReplayDeferred (const std::map<std::string, thcPlugin *> &plugins,
                     const std::string &pluginDir,
                     const std::filesystem::path &dir)
{
    const double at = 20.3, until = 30, window = 1024.0 / 44100;
    std::vector<std::string> errors;
    Peer deferred(plugins, pluginDir, dir, "rd"),
         top(plugins, pluginDir, dir, "rt"),
         still(plugins, pluginDir, dir, "rn");

    deferred.sched.setReplayDeferred(true);

    if (!deferred.load(replayPiece(100), errors) ||
        !still.load(replayPiece(100), errors) ||
        !top.load(replayPiece(50), errors))
    {
        fail("replay deferred", "the piece does not load: " +
             (errors.empty() ? std::string() : errors[0]));
        return;
    }

    play(still, "replay deferred", {}, until, [=] { return window; });
    play(top, "replay deferred", {}, until, [=] { return window; });
    play(deferred, "replay deferred", {}, at, [=] { return window; });

    deferred.sched.knob("v")->setValue(50);
    deferred.sched.knobMoved(deferred.sched.knob("v"), false);

    const double landed = playReplaying(deferred, until, 2);
    const std::string want = notesBetween(top.tape, { 0 }, landed, until);
    const std::string got = notesBetween(deferred.tape, { 0 }, landed, until);
    const std::vector<std::string> waiting =
        lines(notesBetween(deferred.tape, { 0 }, at, landed));
    const std::string queued = notesBetween(still.tape, { 0 }, at, landed);

    if (landed < at + 3 * window)
        fail("replay deferred", "the replay was not spread over windows: "
             "it landed at " + std::to_string(landed));
    else if (!std::all_of(waiting.begin(), waiting.end(),
                          [&](const std::string &l)
                          { return queued.find(l) != std::string::npos; }))
        fail("replay deferred", "before it landed the chain played what it "
             "had not queued");
    else if (want.empty() || got != want)
        fail("replay deferred", "a deferred replay is not heard from where "
             "it landed as the piece from the top: " +
             firstDifference(want, got));
    else if (notesBetween(deferred.tape, { 1 }, 0, until) !=
             notesBetween(still.tape, { 1 }, 0, until))
        fail("replay deferred", "the chain beside it did not play on");
    else
        printf("ok    a deferred replay is heard from where it landed, "
               "%.2f s after the knob moved, and the chain beside it plays "
               "on\n", landed - at);
}

/* What the mirror and the worklet do: one replay advanced all at once and
   one a few ticks a window, landed in the same window, compose the same
   piece -- with a knob the replaying chain reads moved under both on the
   way, and the tempo, neither of which asks for a replay. */
static void
checkReplayHeld (const std::map<std::string, thcPlugin *> &plugins,
                 const std::string &pluginDir,
                 const std::filesystem::path &dir)
{
    const double at = 20.3, until = 30, window = 1024.0 / 44100;
    std::vector<std::string> errors;
    Peer pieces(plugins, pluginDir, dir, "rp"),
         held(plugins, pluginDir, dir, "rh");
    int w = 0;
    auto meddle = [&w](Peer &q)
    {
        if (++w == 4)
        {
            q.sched.knob("h")->setValue(0.005f);
            q.sched.setTempo(132);
        }
    };

    for (Peer *p : { &pieces, &held })
    {
        p->sched.setReplayDeferred(true);

        if (!p->load(replayPiece(100), errors))
        {
            fail("replay held", "the piece does not load");
            return;
        }

        play(*p, "replay held", {}, at, [=] { return window; });
        p->sched.knob("v")->setValue(50);
        p->sched.knobMoved(p->sched.knob("v"), false);
    }

    /* The one in pieces lands when it has caught up; the other is advanced
       all at once and held to land in the same window. */
    const double landAt = playReplaying(pieces, until, 2, -1, meddle);

    w = 0;
    playReplaying(held, until, ~0u, landAt, meddle);

    if (landAt < at + 4 * window)
        fail("replay held", "the replay in pieces landed before anything "
             "moved under it: " + std::to_string(landAt));
    else if (held.tape != pieces.tape)
        fail("replay held", "a replay advanced in pieces is not the one "
             "advanced at once, with a knob and the tempo moved on the way: " +
             firstDifference(held.tape, pieces.tape));
    else
        printf("ok    a replay advanced in pieces is the one advanced at "
               "once, with a knob and the tempo moved on the way\n");
}

/* A breeding population that listens, edited while it plays, through an
   ear that answers at once as the browser's does: what it would have heard
   until then is not rendered again, which would be every generation of it
   inside one edit. */
static void
checkListener (const std::map<std::string, thcPlugin *> &plugins,
               const std::string &pluginDir, const std::filesystem::path &dir)
{
    auto text = [](double mutation)
    {
        return "tempo 120;\n"
               "instrument tone { dsp \"amb01.dsp\"; res = 0.5; };\n"
               "instrument goal { dsp \"amb01.dsp\"; res = 0.9; };\n"
               "preset lo { res = 0.2; };\npreset hi { res = 0.8; };\n"
               "chain search {\n    stage g gen::breed { from = lo; "
               "toward = hi; population = 4; period = 1 s; "
               "target = \"goal\"; listen = 1; mutation = " +
               std::to_string(mutation) +
               "; };\n    sink { instrument = tone; chanarg = \"*\"; };\n"
               "};\n";
    };
    std::vector<std::string> errors;
    Peer p(plugins, pluginDir, dir, "l");

    p.sched.setAuditionSynchronous(true);
    p.load(text(0.2), errors);
    play(p, "listener", {}, 12, [] { return 1024.0 / 44100; });

    const int before = p.sched.auditioner()->answered();

    if (!p.edit(text(0.3), errors))
    {
        fail("listener", "did not load: " +
             (errors.empty() ? "" : errors[0]));
        return;
    }

    const int during = p.sched.auditioner()->answered() - before;

    play(p, "listener", {}, 15, [] { return 1024.0 / 44100; });

    if (before == 0 ||
        p.sched.auditioner()->answered() == before + during)
        fail("listener", "the population never asked the ear, before the "
             "edit or after it");
    else if (during > 0)
        fail("listener", "an edit rendered " + std::to_string(during) +
             " sounds a listening population had already heard");
    else
        printf("ok    an edit does not have a listener hear again what it "
               "heard\n");
}

/* An edit that declares the cue knob at another value says where the knob
   now stands; it is not the knob moving, and cues nothing. */
static void
checkCueEdit (const std::map<std::string, thcPlugin *> &plugins,
              const std::string &pluginDir, const std::filesystem::path &dir)
{
    auto text = [](int act)
    {
        return "tempo 120;\nmeter 4;\n@act = " + std::to_string(act) + ";\n"
               "section calm  1 bars hold { };\n"
               "section storm 1 bars hold { };\n"
               "cue @act 1 bars;\n"
               "chain c { stage s gen::euclid { steps = 4; fills = 4;\n"
               "    period = 1 beats; hold = 0.2 beats; };\n"
               "    sink { channel = 1; }; };\n";
    };
    std::vector<std::string> errors;
    Peer p(plugins, pluginDir, dir, "cue");

    p.load(text(0), errors);
    play(p, "cue edit", {}, 3, [] { return 0.02; });

    if (!p.edit(text(2), errors))
    {
        fail("cue edit", "did not load: " + (errors.empty() ? "" : errors[0]));
        return;
    }

    play(p, "cue edit", {}, 7, [] { return 0.02; });

    if (p.sched.sectionAt(6.5) != 0)
        fail("cue edit", "an edit to @act's declared value cued a section");
    else
        printf("ok    an edit to the cue knob's declaration cues nothing\n");
}

/* A Life board painted on while it plays, and then a param of its chain
   set: what was painted is still on the board, which a chain played again
   from its text would have lost. */
static void
checkPainted (const std::map<std::string, thcPlugin *> &plugins,
              const std::string &pluginDir, const std::filesystem::path &dir)
{
    const std::string text =
        "tempo 120;\nchain c {\n    stage b gen::life { width = 4; "
        "height = 4; wrap = 1; board = \"..../..../..../....\"; "
        "notes = \"C4 D4 E4 G4\"; period = 1 s; hold = 0.2 s; };\n"
        "    sink { channel = 1; };\n};\n";
    const double at = 3, until = 12;
    std::string tapes[2];

    for (int set = 0; set < 2; set++)
    {
        std::vector<std::string> errors;
        Peer p(plugins, pluginDir, dir, "paint");

        p.load(text, errors);
        play(p, "painted", {}, at, [] { return 1024.0 / 44100; });

        /* A row of three, which stays alive: a blinker. */
        thcStage *s = p.sched.chain(0)->stages[0].get();
        const thcInputType kinds[] = { THC_IN_PRESS, THC_IN_DRAG,
                                       THC_IN_DRAG, THC_IN_RELEASE };

        for (int i = 0; i < 4; i++)
        {
            thcInputEvent ev = {};

            ev.type = kinds[i];
            ev.x = 50 + 100 * std::min(i, 2);
            ev.y = 150;
            ev.w = ev.h = 400;
            ev.button = 1;
            thcScheduler::input(s, ev);
        }

        if (set == 1)
            p.sched.recompose(0);

        play(p, "painted", {}, until, [] { return 1024.0 / 44100; });
        tapes[set] = notesBetween(p.tape, { 0 }, at, until);
    }

    if (tapes[0].empty())
        fail("painted", "a painted board played nothing");
    else if (tapes[1] != tapes[0])
        fail("painted", "a param set lost what was painted: " +
             firstDifference(tapes[0], tapes[1]));
    else
        printf("ok    a param set keeps what was painted on its chain\n");
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
    void noteOn (int, int, int, float, int64_t) override {}
    void noteOff (int, int, int64_t) override {}
    void control (int, const std::string &, double, int64_t) override {}
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

/* A chain riding @k through `values', two seconds a step at 120. */
static std::string
rider (const std::string &values)
{
    return "chain w {\n    stage s gen::steps { values = \"" + values +
           "\"; period = 4 beats; min = 0; max = 16; };\n"
           "    sink { knob = @k; };\n};\n";
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

    /* A tune composed whole at its first tick, with a level reading @k
       through arithmetic, which the loader builds as an embedded node. */
    auto ahead = [](const std::string &note)
    {
        return "chain ahead {\n    stage src gen::lsystem { axiom = "
               "\"F_F_F_F_F_F_F_F_F_F_F_F_\"; rules = \"\"; depth = 0; "
               "notes = \"" + note + "\"; step = 0.5 beats; "
               "hold = 0.25 beats; vel = 100; };\n"
               "    stage l xform::level { gain = 0.5 + @k * 0; };\n"
               "    sink { channel = 1; };\n};\n";
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
        { "a knob a rebuilt chain writes is where the new text has it",
          head + rider("0.1 0.2 0.3 0.4"), head + rider("0.5 0.6 0.7 0.8"),
          5.01, nullptr,
          [](Peer &p)
          {
              p.stepTo(5.02);

              const float k = (*p.sched.knob("k"))[0];

              return std::fabs(k - 0.7f * 16) < 1e-3
                         ? std::string()
                         : "@k is " + std::to_string(k) + ", not " +
                               std::to_string(0.7 * 16);
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
        { "a chain with nodes built again plays what its text plays from "
          "the top",
          head + ahead("C3"), head + ahead("D3"), 8.21, nullptr,
          [&](Peer &p)
          {
              Peer top(plugins, pluginDir, dir, "top");

              top.load(head + ahead("D3"), errors);
              top.sched.start();
              top.stepTo(12);
              p.stepTo(12);

              const std::string
                  want = notesBetween(top.tape, { 0 }, 8.21, 12),
                  got = notesBetween(p.tape, { 0 }, 8.21, 12);

              return !want.empty() && got == want
                         ? std::string()
                         : firstDifference(want, got);
          } },
        { "a rewind puts a written knob at its edited declaration",
          head + "@w = 0.25;\n" + writer, head + "@w = 0.5;\n" + writer,
          1.01, nullptr, homeIs(0.5f) },
        { "a rewind puts a written knob at its declaration, not at Play's",
          head + "@w = 0.25;\n" + writer,
          "# a comment\n" + head + "@w = 0.25;\n" + writer, 1.01,
          [](Peer &p) { p.sched.knob("w")->setValue(0.9f); }, homeIs(0.25f) },
        { "a chain's new seed builds its stages again, and only its",
          head + smallChain("a", 1, "    seed 7;\n") + smallChain("b", 2, ""),
          head + smallChain("a", 1, "    seed 8;\n") + smallChain("b", 2, ""),
          1.01, nullptr,
          [](Peer &p)
          {
              /* A stage built by the edit has not played yet. */
              auto played = [&p](size_t c)
              {
                  return p.sched.chain(c)->stages[0]->lastOut >= 0;
              };

              if (played(0))
                  return std::string("the reseeded chain kept its stage");

              return played(1) ? std::string()
                               : std::string("the other chain was built "
                                             "again");
          } },
        { "a new piece seed leaves a chain with its own playing",
          "seed 1;\n" + head + smallChain("a", 1, "    seed 7;\n") +
          smallChain("b", 2, ""),
          "seed 2;\n" + head + smallChain("a", 1, "    seed 7;\n") +
          smallChain("b", 2, ""),
          1.01, nullptr,
          [](Peer &p)
          {
              auto played = [&p](size_t c)
              {
                  return p.sched.chain(c)->stages[0]->lastOut >= 0;
              };

              if (!played(0))
                  return std::string("the seeded chain was built again");

              return played(1) ? std::string("the other chain kept its "
                                             "stage")
                               : std::string();
          } },
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
    checkRecompose(plugins, pluginDir, dir);
    checkKnobMoved(plugins, pluginDir, dir);
    checkReplayDeferred(plugins, pluginDir, dir);
    checkReplayHeld(plugins, pluginDir, dir);
    checkListener(plugins, pluginDir, dir);
    checkPainted(plugins, pluginDir, dir);
    checkCueEdit(plugins, pluginDir, dir);

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
