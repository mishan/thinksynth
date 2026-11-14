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

#include "NodeHistory.h"

#include <map>
#include <set>

#include "NodeGraph.h"

using std::string;
using std::vector;

NodeHistory::NodeHistory (size_t cap, double joinSeconds)
    : cap_(cap), joinSeconds_(joinSeconds)
{
}

void NodeHistory::push (vector<Step> &to, const Snapshot &state,
                        const string &what)
{
    Step s;

    s.state = state;
    s.what = what;
    s.at = 0;

    to.push_back(s);
}

void NodeHistory::record (const Snapshot &was, const string &what,
                          const string &join, double now)
{
    future_.clear();

    if (!join.empty() && !past_.empty() && past_.back().join == join &&
        now >= past_.back().at && now - past_.back().at <= joinSeconds_)
    {
        past_.back().at = now;
        return;
    }

    push(past_, was, what);
    past_.back().join = join;
    past_.back().at = now;

    if (past_.size() > cap_)
        past_.erase(past_.begin(), past_.end() - cap_);
}

const string &NodeHistory::undoName (void) const
{
    static const string none;

    return past_.empty() ? none : past_.back().what;
}

const string &NodeHistory::redoName (void) const
{
    static const string none;

    return future_.empty() ? none : future_.back().what;
}

/* The step taken goes to the other list under the same name, holding the
   state it was taken from. Neither list keeps a join key past this, so
   whatever is done next is a step of its own. */
bool NodeHistory::undo (const Snapshot &current, Snapshot &out)
{
    if (past_.empty())
        return false;

    const Snapshot shown = current;

    out = past_.back().state;
    push(future_, shown, past_.back().what);
    past_.pop_back();

    if (!past_.empty())
        past_.back().join.clear();

    return true;
}

bool NodeHistory::redo (const Snapshot &current, Snapshot &out)
{
    if (future_.empty())
        return false;

    const Snapshot shown = current;

    out = future_.back().state;
    push(past_, shown, future_.back().what);
    future_.pop_back();

    return true;
}

void NodeHistory::clear (void)
{
    past_.clear();
    future_.clear();
}

/* Every box but the panels, by what it is; every wire, by its ends. Two
   graphs that differ here differ by more than values. */
static std::multiset<string> shapeOf (const NodeGraph &g)
{
    std::multiset<string> out;

    for (const NodeGraph::Box &b : g.boxes())
        if (!b.isProbe)
            out.insert(b.name + (b.isIoSource ? "#in" : "") +
                       (b.isIoSink ? "#out" : "") +
                       (b.isControl ? " control" : "") +
                       (b.isExpr ? " = " + b.exprText : ""));

    for (const NodeGraph::Edge &e : g.edges())
        out.insert(g.boxes()[e.fromBox].name + "." +
                   g.boxes()[e.fromBox].ports[e.fromPort].name + " -> " +
                   g.boxes()[e.toBox].name + "." +
                   g.boxes()[e.toBox].ports[e.toPort].name);

    return out;
}

/* By name, since the two graphs number their boxes differently -- and an io
   node is two boxes under one name, its values split between them. Every
   value counts, outputs included: the io node's constants are outputs to
   the nodes that read them, and a plugin's own outputs never differ between
   two parses. */
vector<NodeHistory::LiveChange> NodeHistory::liveChanges (
    const NodeGraph &before, const NodeGraph &after, bool &complete)
{
    complete = shapeOf(before) == shapeOf(after);

    std::map<string, double> controls, values;
    std::set<string> slid;

    for (const NodeGraph::Box &b : before.boxes())
    {
        if (b.isControl)
            controls[b.ctlArg] = b.ctlValue;
        else if (!b.isExpr && !b.isProbe)
            for (const NodeGraph::Param &p : b.params)
                if (p.hasValue && (p.kind == NodeGraph::Param::VALUE ||
                                   p.kind == NodeGraph::Param::CHANARG))
                    values[b.name + "." + p.name] = p.value;
    }

    for (const NodeGraph::Box &b : after.boxes())
        if (b.isControl)
            slid.insert("@" + b.ctlArg);

    vector<LiveChange> out;

    for (const NodeGraph::Box &b : after.boxes())
    {
        if (b.isControl)
        {
            const auto was = controls.find(b.ctlArg);

            if (was != controls.end() && was->second != b.ctlValue)
                out.push_back({ true, "", b.ctlArg, b.ctlValue });

            continue;
        }

        if (b.isExpr || b.isProbe)
            continue;

        for (const NodeGraph::Param &p : b.params)
        {
            if (!p.hasValue)
                continue;

            const auto was = values.find(b.name + "." + p.name);

            if (was == values.end() || was->second == p.value)
                continue;

            /* A control's reader follows the control; a @name nothing slides
               has nothing to tell. */
            if (p.kind == NodeGraph::Param::CHANARG)
            {
                if (!slid.count(p.source))
                    complete = false;
            }
            else if (p.kind == NodeGraph::Param::VALUE)
                out.push_back({ false, b.name, p.name, p.value });
        }
    }

    return out;
}
