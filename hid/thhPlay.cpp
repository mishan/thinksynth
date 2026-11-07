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
#include <functional>
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
        else if (a.kind == thhAction::LOOP)
            continue;
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

/* A patch's lines, while its block is checked against the player's file:
   lines of their own, so a mistake on one is the patch's whatever line of
   the file has the same number. */
#define THH_PATCH_LINES 1000000

/* A patch's block's bindings, marked so the pane can say whose they are,
   on the patch's lines of their own. */
static void
fromPatch (thcHidBlock &b)
{
    b.line += THH_PATCH_LINES;
    for (auto &i : b.ignore)
        i.second += THH_PATCH_LINES;
    for (thcHidBinding &x : b.bindings)
    {
        x.patch = true;
        x.line += THH_PATCH_LINES;
    }
    for (thcHidLayer &l : b.layers)
    {
        l.line += THH_PATCH_LINES;
        l.whenLine += THH_PATCH_LINES;
        for (thcHidBinding &x : l.bindings)
        {
            x.patch = true;
            x.line += THH_PATCH_LINES;
        }
    }
}

/* What plays, on the patch's lines as the patch numbers them. */
static void
patchLines (thcHidBlock &b)
{
    const auto back = [](int &line) {
        if (line >= THH_PATCH_LINES)
            line -= THH_PATCH_LINES;
    };

    back(b.line);
    for (thcHidBinding &x : b.bindings)
        back(x.line);
    for (thcHidLayer &l : b.layers)
    {
        back(l.line);
        back(l.whenLine);
        for (thcHidBinding &x : l.bindings)
            back(x.line);
    }
}

/* over on top of into: a control bound in over replaces every binding of
   it in into, a layer replaces the one of its name, an ignore drops
   either, and a setting over sets is over's. False if strict and over
   ignores what isn't there: a hand's own block may have dropped it
   already, so only what one controller alone plays, which has every
   control the player's file could name, says so. */
static bool
overlay (thcHidBlock &into, const thcHidBlock &over, bool strict,
         const std::function<bool (const std::string &)> &known,
         std::string &err, int &line)
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
        if (!drop(over.ignore[i].first) &&
            (strict || !known(over.ignore[i].first)))
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
         bool own, bool patch, bool strict,
         const std::function<bool (const std::string &)> &known,
         thcHidBlock &into, std::string &err, int &line)
{
    for (int pass = 0; pass < (hand.empty() ? 1 : 2); pass++)
        for (size_t i = 0; i < blocks.size(); i++)
            if (blocks[i].hand == (pass ? hand : std::string()))
            {
                thcHidBlock b = blocks[i];

                if (own)
                    owned(b);
                if (patch)
                    fromPatch(b);
                into.device = b.device;
                into.line = b.line;
                if (!overlay(into, b, strict, known, err, line))
                    return false;
            }
    return true;
}

bool
thhLoadProfile (const std::string &path, const thhControls &controls,
                const char *fallback, const std::string &hand,
                thcHidBlock &out, std::string &err, const std::string &patch,
                const std::string &patchName, std::string *patchErr)
{
    std::error_code ec;
    std::string ignored, why;
    std::vector<thcHidBlock> ownBlocks, mine, theirs;
    int line = 0, at = 0;

    err.clear();
    thcHidParseProfiles(fallback, ownBlocks, ignored, line);

    /* What an ignore may name, in a hand that has dropped it already: a
       control the device has, or a layer any of the profiles declares. */
    const auto known = [&](const std::string &name) {
        if (controls.button(name) >= 0 || controls.axis(name) >= 0)
            return true;
        for (const auto *blocks : { &ownBlocks, &mine, &theirs })
            for (const thcHidBlock &b : *blocks)
                for (const thcHidLayer &l : b.layers)
                    if (l.name == name)
                        return true;
        return false;
    };
    /* A hand's: the device's own, then the player's file, then the
       patch's. Only one controller alone has every control the player's
       file could name, so only it says an ignore of nothing is wrong; and
       a patch may drop what the file already has. */
    const auto forOne = [&](const std::string &h, bool player, bool patched,
                            thcHidBlock &into) {
        thcHidBlock own;

        forHand(ownBlocks, h, true, false, h.empty(), known, own, ignored, line);
        into = own;
        return (!player || forHand(mine, h, false, false, h.empty(), known, into, why, at)) &&
               (!patched || forHand(theirs, h, false, true, false, known, into, why, at));
    };
    /* Every hand, whichever is asked for now: an error in a hand's block
       is said with one controller too. */
    thcHidBlock both;
    const auto fits = [&](bool player, bool patched) {
        thhMapper check;
        std::vector<thhAction> none;

        for (const char *h : { "", "left", "right" })
            if (!forOne(h, player, patched, both) ||
                !check.configure(both, controls, none, why, at))
                return false;
        return true;
    };
    const auto where = [&](const std::string &file) {
        return at < 0 ? "the controller's own profile, line " +
                        std::to_string(-at) + ": " + why
                      : file + ":" + std::to_string(at) + ": " + why;
    };
    bool player = false, patched = false;

    if (std::filesystem::exists(path, ec))
    {
        std::ifstream in(path.c_str());
        std::ostringstream text;

        if (std::filesystem::is_regular_file(path, ec))
            text << in.rdbuf();
        if (!in || !std::filesystem::is_regular_file(path, ec))
            err = path + ": can't read it";
        else if (!thcHidParseProfiles(text.str(), mine, why, at) ||
                 !fits(true, false))
            err = where(path);
        else
            player = true;
    }

    /* The patch's, over what of the file fits; a patch that doesn't fit
       is left out, and the rest plays. A clash it makes with the file may
       be said at the file's line. */
    std::string theirErr;

    if (!patch.empty())
    {
        const bool parsed = thcHidParseProfiles(patch, theirs, why, at);

        patched = parsed && fits(player, true);
        if (!patched && parsed && at > 0 && at < THH_PATCH_LINES)
            theirErr = where(path) + ", with " + patchName;
        else if (!patched)
        {
            if (at >= THH_PATCH_LINES)
                at -= THH_PATCH_LINES;
            theirErr = where(patchName);
        }
    }
    err += (err.empty() || theirErr.empty() ? "" : "; ") + theirErr;

    forOne(hand, player, patched, out);
    patchLines(out);
    if (patchErr)
        *patchErr = theirErr;
    return err.empty();
}
