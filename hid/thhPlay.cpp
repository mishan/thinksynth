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

/* The player's changes on the controller's own profile: a control bound in
   mine replaces every binding of it in own, a layer replaces the one of its
   name, an ignore drops either, and a setting mine sets is mine. Own's
   lines go negative, so an error in what is left of it can say so. False
   if mine ignores what isn't there. */
static bool
overlay (const thcHidBlock &own, const thcHidBlock &mine, thcHidBlock &out,
         std::string &err, int &line)
{
    out = own;
    for (size_t i = 0; i < out.bindings.size(); i++)
        out.bindings[i].line = -out.bindings[i].line;
    for (size_t l = 0; l < out.layers.size(); l++)
    {
        out.layers[l].line = -out.layers[l].line;
        out.layers[l].whenLine = -out.layers[l].whenLine;
        for (size_t i = 0; i < out.layers[l].bindings.size(); i++)
            out.layers[l].bindings[i].line = -out.layers[l].bindings[i].line;
    }

    if (mine.hasChannel)
        out.channel = mine.channel;
    if (mine.hasBase)
        out.base = mine.base;
    if (mine.hasVelocity)
        out.velocity = mine.velocity;

    const auto drop = [&out](const std::string &name) {
        const size_t had = out.bindings.size() + out.layers.size();

        for (size_t i = out.bindings.size(); i-- > 0;)
            if (out.bindings[i].control == name)
                out.bindings.erase(out.bindings.begin() + i);
        for (size_t l = out.layers.size(); l-- > 0;)
            if (out.layers[l].name == name)
                out.layers.erase(out.layers.begin() + l);
        return out.bindings.size() + out.layers.size() < had;
    };

    for (size_t i = 0; i < mine.ignore.size(); i++)
        if (!drop(mine.ignore[i].first))
        {
            err = "the controller's own profile has no '" +
                  mine.ignore[i].first + "' to ignore";
            line = mine.ignore[i].second;
            return false;
        }

    for (size_t i = 0; i < mine.bindings.size(); i++)
        for (size_t k = out.bindings.size(); k-- > 0;)
            if (out.bindings[k].control == mine.bindings[i].control)
                out.bindings.erase(out.bindings.begin() + k);
    out.bindings.insert(out.bindings.end(), mine.bindings.begin(),
                        mine.bindings.end());

    for (size_t l = 0; l < mine.layers.size(); l++)
    {
        size_t k = 0;

        while (k < out.layers.size() && out.layers[k].name != mine.layers[l].name)
            k++;
        if (k < out.layers.size())
            out.layers[k] = mine.layers[l];
        else
            out.layers.push_back(mine.layers[l]);
    }

    return true;
}

bool
thhLoadProfile (const std::string &path, const thhControls &controls,
                const char *fallback, thcHidBlock &out, std::string &err)
{
    std::error_code ec;
    std::string ignored;
    thcHidBlock own;
    int line = 0;

    err.clear();
    thcHidParseProfile(fallback, own, ignored, line);
    out = own;

    if (!std::filesystem::exists(path, ec))
        return true;

    std::ifstream in(path.c_str());
    std::ostringstream text;
    thcHidBlock mine, both;
    thhMapper check;
    std::vector<thhAction> none;

    text << in.rdbuf();
    if (!in)
        err = path + ": can't read it";
    else if (thcHidParseProfile(text.str(), mine, err, line) &&
             overlay(own, mine, both, err, line) &&
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
