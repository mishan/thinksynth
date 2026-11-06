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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "thhPlay.h"

void
thhPlay (thSynth &synth, const std::vector<thhAction> &acts,
         const std::function<void (const thhAction &)> &heard)
{
    for (size_t i = 0; i < acts.size(); i++)
    {
        const thhAction &a = acts[i];

        if (a.kind == thhAction::NOTE_ON)
            synth.addNote(a.channel, a.note, a.vel);
        else if (a.kind == thhAction::NOTE_OFF)
            synth.delNote(a.channel, a.note);
        else if (a.kind == thhAction::NOTE_BEND)
        {
            synth.bendNote(a.channel, a.note, (float)a.t);
            continue;
        }
        else if (a.kind == thhAction::NOTE_AUX)
        {
            synth.setNoteAux(a.channel, a.note, a.aux, (float)a.t);
            continue;
        }
        else
        {
            /* Looked up every time: a patch loaded since would leave a kept
               pointer pointing at an arg that has gone. */
            thArg *arg = synth.getChanArg(a.channel, a.arg);

            /* setValue is the GUI thread's only for a one-element arg. */
            if (arg && arg->len() == 1)
                arg->setValue((float)thhActionValue(a, arg->min(), arg->max()));
            continue;
        }

        if (heard)
            heard(a);
    }
}

/* A block's lines made negative: the device's own, so an error in what is
   left of it can say so, and the pane can tell it from the player's. */
static void
owned (thcHidBlock &b)
{
    for (size_t i = 0; i < b.bindings.size(); i++)
        b.bindings[i].line = -std::abs(b.bindings[i].line);
    for (size_t l = 0; l < b.layers.size(); l++)
    {
        b.layers[l].line = -std::abs(b.layers[l].line);
        b.layers[l].whenLine = -std::abs(b.layers[l].whenLine);
        for (size_t i = 0; i < b.layers[l].bindings.size(); i++)
            b.layers[l].bindings[i].line = -std::abs(b.layers[l].bindings[i].line);
    }
}

/* over on top of into: a control bound in over replaces every binding of
   it in into, a layer replaces the one of its name, an ignore drops
   either, and a setting over sets is over's. False if over ignores what
   isn't there. */
static bool
overlay (thcHidBlock &into, const thcHidBlock &over, std::string &err,
         int &line)
{
    if (over.hasChannel)
        into.channel = over.channel;
    if (over.hasBase)
        into.base = over.base;
    if (over.hasVelocity)
        into.velocity = over.velocity;

    const auto drop = [&into](const std::string &name) {
        const size_t had = into.bindings.size() + into.layers.size();

        for (size_t i = into.bindings.size(); i-- > 0;)
            if (into.bindings[i].control == name)
                into.bindings.erase(into.bindings.begin() + i);
        for (size_t l = into.layers.size(); l-- > 0;)
            if (into.layers[l].name == name)
                into.layers.erase(into.layers.begin() + l);
        return into.bindings.size() + into.layers.size() < had;
    };

    for (size_t i = 0; i < over.ignore.size(); i++)
        if (!drop(over.ignore[i].first))
        {
            err = "there is no '" + over.ignore[i].first + "' to ignore";
            line = over.ignore[i].second;
            return false;
        }

    for (size_t i = 0; i < over.bindings.size(); i++)
        for (size_t k = into.bindings.size(); k-- > 0;)
            if (into.bindings[k].control == over.bindings[i].control)
                into.bindings.erase(into.bindings.begin() + k);
    into.bindings.insert(into.bindings.end(), over.bindings.begin(),
                         over.bindings.end());

    for (size_t l = 0; l < over.layers.size(); l++)
    {
        size_t k = 0;

        while (k < into.layers.size() && into.layers[k].name != over.layers[l].name)
            k++;
        if (k < into.layers.size())
            into.layers[k] = over.layers[l];
        else
            into.layers.push_back(over.layers[l]);
    }

    return true;
}

/* The blocks of a profile for one hand, either hand's first. */
static bool
forHand (const std::vector<thcHidBlock> &blocks, const std::string &hand,
         bool own, thcHidBlock &into, std::string &err, int &line)
{
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < blocks.size(); i++)
            if (blocks[i].hand == (pass ? hand : std::string()))
            {
                thcHidBlock b = blocks[i];

                if (own)
                    owned(b);
                into.device = b.device;
                if (!overlay(into, b, err, line))
                    return false;
            }
    return true;
}

bool
thhLoadProfile (const std::string &path, const thhControls &controls,
                const char *fallback, const std::string &hand,
                thcHidBlock &out, std::string &err)
{
    std::error_code ec;
    std::string ignored;
    std::vector<thcHidBlock> ownBlocks;
    thcHidBlock own;
    int line = 0;

    err.clear();
    thcHidParseProfiles(fallback, ownBlocks, ignored, line);
    forHand(ownBlocks, hand, true, own, ignored, line);
    out = own;

    if (!std::filesystem::exists(path, ec))
        return true;

    std::ifstream in(path.c_str());
    std::ostringstream text;
    std::vector<thcHidBlock> mine;
    thcHidBlock both = own;
    thhMapper check;
    std::vector<thhAction> none;

    text << in.rdbuf();
    if (!in)
        err = path + ": can't read it";
    else if (thcHidParseProfiles(text.str(), mine, err, line) &&
             forHand(mine, hand, false, both, err, line) &&
             check.configure(both, controls, none, err, line))
    {
        out = both;
        return true;
    }
    else
        err = line < 0 ? "the controller's own profile, line " +
                         std::to_string(-line) + ": " + err
                       : path + ":" + std::to_string(line) + ": " + err;

    return false;
}
