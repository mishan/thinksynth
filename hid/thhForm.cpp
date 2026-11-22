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

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "thhForm.h"
#include "thhProfile.h"
#include "thhProfileEdit.h"

namespace {

/* The blocks of `text', if it reads and they are all `device''s. */
bool
blocksOf (const std::string &text, const std::string &device,
          std::vector<thHidBlock> &out, std::string &err)
{
    int line = 0;

    if (!thHidParseProfiles(text, out, err, line))
    {
        err = "line " + std::to_string(line) + ": " + err;
        out.clear();
        return false;
    }
    for (const thHidBlock &b : out)
        if (b.device != device)
        {
            err = "it is for '" + b.device + "', not '" + device + "'";
            out.clear();
            return false;
        }
    return true;
}

thHidBlock
eitherHand (const std::vector<thHidBlock> &blocks)
{
    for (const thHidBlock &b : blocks)
        if (b.hand.empty())
            return b;
    return thHidBlock();
}

std::vector<thHidBinding>
boundTo (const std::vector<thHidBinding> &bindings, const std::string &control)
{
    std::vector<thHidBinding> out;

    for (const thHidBinding &b : bindings)
        if (b.control == control)
            out.push_back(b);
    return out;
}

bool
ignores (const thHidBlock &blk, const std::string &control)
{
    for (const auto &i : blk.ignore)
        if (i.first == control)
            return true;
    return false;
}

bool
number (const std::string &v, double &out)
{
    char *end;

    out = strtod(v.c_str(), &end);
    return !v.empty() && *end == '\0' && std::isfinite(out);
}

/* `key=value' made so in `b'. */
bool
field (thHidBinding &b, const std::string &key, const std::string &value,
       const std::vector<std::string> &layers, std::string &err)
{
    double v = 0;
    const bool none = value.empty() && (key == "min" || key == "max");

    if (key == "kind")
    {
        for (const thhFormKind &k : thhFormKinds())
            if (value == k.name)
            {
                const std::string control = b.control;

                b = thHidBinding();
                b.control = control;
                b.kind = k.kind;
                b.range = 12;
                b.semis = k.kind == thHidBinding::OCTAVE;
                b.arg = k.kind == thHidBinding::LOOP ? "record" : "";
                b.layer = layers.empty() ? "" : layers[0];
                return true;
            }
        err = "'" + value + "' is not an action";
        return false;
    }
    if (key == "layer" || key == "arg")
    {
        (key == "layer" ? b.layer : b.arg) = value;
        return true;
    }
    if (key == "shape" || key == "step")
    {
        if (key == "shape")
            b.shape = value == "exp" ? thHidBinding::EXP : thHidBinding::LINEAR;
        else
            b.octaves = value == "octave";
        return true;
    }
    if (key == "scale")
    {
        const std::vector<int> *steps = thHidScaleSteps(value);

        if (steps == NULL && !value.empty())
        {
            err = "there is no scale '" + value + "'";
            return false;
        }
        b.scale = steps == NULL ? std::vector<int>() : *steps;
        return true;
    }
    if (!none && !number(value, v))
    {
        err = "'" + value + "' isn't a number for " + key;
        return false;
    }
    if (key == "semis" || key == "channel" || key == "range")
    {
        if (v < INT_MIN || v > INT_MAX || v != (int)v)
        {
            err = key + " is a whole number";
            return false;
        }
        (key == "semis" ? b.semis : key == "channel" ? b.channel : b.range) = (int)v;
    }
    else if (key == "min" || key == "max")
    {
        (key == "min" ? b.hasMin : b.hasMax) = !none;
        (key == "min" ? b.min : b.max) = v;
    }
    else if (key == "at")
    {
        /* An off nobody gave follows at, as the parser has it. */
        if (b.off == std::max(0.0, b.at - 0.2))
            b.off = std::max(0.0, v - 0.2);
        b.at = v;
    }
    else if (key == "off" || key == "center" || key == "smooth")
        (key == "off" ? b.off : key == "center" ? b.center : b.smooth) = v;
    else if (key == "latch" || key == "both")
        (key == "latch" ? b.latch : b.both) = v != 0;
    else
    {
        err = "a binding has no " + key;
        return false;
    }
    return true;
}

bool
fields (thHidBinding &b, const std::string &lines,
        const std::vector<std::string> &layers, std::string &err)
{
    std::istringstream in(lines);
    std::string line;

    while (std::getline(in, line))
    {
        const size_t eq = line.find('=');

        if (eq == std::string::npos ||
            !field(b, line.substr(0, eq), line.substr(eq + 1), layers, err))
        {
            if (eq == std::string::npos)
                err = "'" + line + "' is not key=value";
            return false;
        }
    }
    return true;
}

}

const std::vector<thhFormKind> &
thhFormKinds ()
{
    using K = thHidBinding;
    static const std::vector<thhFormKind> kinds = {
        { "note", K::NOTE, true, false, "semis" },
        { "hold", K::HOLD, true, false, "layer" },
        { "toggle", K::TOGGLE, true, false, "layer" },
        { "octave", K::OCTAVE, true, false, "semis" },
        { "value", K::VALUE, false, true, "arg" },
        { "pitch", K::PITCH, false, true, "" },
        { "velocity", K::VELOCITY, false, true, "" },
        { "strike", K::STRIKE, false, true, "semis" },
        { "bend", K::BEND, false, true, "" },
        { "aux", K::AUX, false, true, "semis" },
        { "loop", K::LOOP, true, true, "arg" },
    };

    return kinds;
}

const char *
thhFormKindName (thHidBinding::Kind kind)
{
    for (const thhFormKind &k : thhFormKinds())
        if (k.kind == kind)
            return k.name;
    return "";
}

thhForm
thhFormOf (const thhControls &controls, const char *fallback,
           const std::string &text, const std::string &patch)
{
    thhForm form;
    std::vector<thHidBlock> owns, mines, patches;
    std::string ignored;

    blocksOf(fallback, controls.device, owns, ignored);
    blocksOf(patch, controls.device, patches, ignored);
    if (text.find_first_not_of(" \t\r\n") != std::string::npos &&
        !blocksOf(text, controls.device, mines, form.problem))
        form.problem = "your profile doesn't read (" + form.problem + ")";

    const thHidBlock own = eitherHand(owns), mine = eitherHand(mines);

    for (const thHidLayer &l : mine.layers)
        form.layers.push_back(l.name);

    std::vector<std::pair<std::string, bool>> all;

    for (const std::string &b : controls.buttons)
        all.push_back({ b, false });
    for (const auto &a : controls.axes)
        all.push_back({ a.name, true });
    for (const auto &[name, axis] : all)
    {
        thhFormControl c = {};

        c.control = name;
        c.axis = axis;
        c.bindings = boundTo(mine.bindings, name);
        c.yours = !c.bindings.empty();
        c.off = !c.yours && ignores(mine, name);
        c.own = !boundTo(own.bindings, name).empty();
        if (!c.yours && !c.off)
            c.bindings = boundTo(own.bindings, name);
        for (const thHidBlock &blk : patches)
            c.patched |= !boundTo(blk.bindings, name).empty() || ignores(blk, name);
        for (const auto *blocks : { &mines, &owns, &patches })
            for (const thHidBlock &blk : *blocks)
            {
                const std::string whose = blocks == &patches ? "the patch's " : "";
                const std::string hand = blk.hand.empty() ? "" : whose + blk.hand + " hand";

                /* The player's binding for either hand replaces a hand's own
                   too. */
                const std::string under = blocks == &owns && (c.yours || c.off)
                                        ? " (yours plays instead)" : "";

                for (const thHidBinding &b : hand.empty()
                         ? std::vector<thHidBinding>() : boundTo(blk.bindings, name))
                    c.elsewhere.push_back(hand + ": " + thhBindingText(b) + under);
                if (!hand.empty() && ignores(blk, name))
                    c.elsewhere.push_back(hand + ": ignore " + name + ";" + under);
                for (const thHidLayer &l : blk.layers)
                    for (const thHidBinding &b : boundTo(l.bindings, name))
                        c.elsewhere.push_back((hand.empty() ? whose : hand + ", ") +
                                              "layer " + l.name + ": " +
                                              thhBindingText(b));
            }
        form.controls.push_back(c);
    }
    return form;
}

bool
thhFormEdit (std::string &text, const thhControls &controls,
             const char *fallback, const std::string &op,
             const std::string &control, int which,
             const std::string &lines, std::string &err)
{
    const thhForm form = thhFormOf(controls, fallback, text, "");
    const thhFormControl *c = NULL;

    err = form.problem;
    if (!err.empty())
        return false;
    for (const thhFormControl &fc : form.controls)
        if (fc.control == control)
            c = &fc;
    if (c == NULL)
    {
        err = "the " + controls.device + " has no '" + control + "'";
        return false;
    }

    std::vector<thHidBinding> want = c->bindings;
    const bool one = which >= 0 && which < (int)want.size();
    std::string t = text;
    const std::string &device = controls.device;
    bool ok = true;

    if (op == "set" || op == "add")
    {
        thHidBinding fresh;

        fresh.control = control;
        if (op == "set" && !one)
        {
            err = "there is no such binding";
            return false;
        }
        if (op == "add")
            want.push_back(fresh);
        if (!fields(want[op == "set" ? which : want.size() - 1], lines,
                    form.layers, err))
            return false;
    }
    else if (op == "remove" && one)
        want.erase(want.begin() + which);
    else if (op == "reset")
        ok = thhProfileReset(t, device, control, err);
    else if (op == "off")
        ok = c->own ? thhProfileOff(t, device, control, err)
                    : thhProfileReset(t, device, control, err);
    else
    {
        err = op == "remove" ? "there is no such binding" : "'" + op + "' is not an edit";
        return false;
    }

    if (op == "reset" || op == "off")
        ;
    /* The player's last taken out is off, not the device's own back. */
    else if (c->yours && op == "remove" && want.empty() && c->own)
        ok = thhProfileOff(t, device, control, err);
    else if (c->yours && op == "remove")
        ok = thhProfileRemove(t, device, control, which, err);
    else if (c->yours)
        ok = thhProfileSet(t, device, want[op == "set" ? which : want.size() - 1],
                           op == "set" ? which : -1, err);
    /* The device's own, written over as the player's: all of them, as a
       control the player binds is theirs alone, and none an ignore. */
    else if (want.empty())
        ok = thhProfileOff(t, device, control, err);
    else
    {
        ok = !c->off || thhProfileReset(t, device, control, err);
        for (size_t j = 0; ok && j < want.size(); j++)
            ok = thhProfileSet(t, device, want[j], (int)j, err);
    }

    thHidBlock plays;

    if (!ok || !thhLoadProfileText(t, "your profile", controls, fallback, "",
                                   plays, err))
        return false;
    text = t;
    return true;
}
