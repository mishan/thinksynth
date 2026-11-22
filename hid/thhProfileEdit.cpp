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
#include <charconv>
#include <vector>

#include "thTextEdit.h"
#include "thhProfileEdit.h"

namespace {

/* As few digits as read back as the same double, and never an exponent,
   which the profile's numbers don't take: so room for any double's, the
   largest's 309 digits and the smallest's 324 places. */
std::string
number (double v)
{
    char buf[400];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v,
                                 std::chars_format::fixed);

    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::string();
}

/* `text' read by the lexer as one token of `kind' that is `s', so a name
   can't carry a statement of its own into the profile. */
bool
reads (const std::string &text, thWordToken::Kind kind, const std::string &s)
{
    std::vector<thWordToken> toks;
    std::string err;
    int line;

    return thWordTokenize(text, toks, err, line) && toks.size() == 2 &&
           toks[0].kind == kind && toks[0].text == s;
}

/* The player's block for either hand of `device', from `text': false if the
   text doesn't read, found false if there is none. */
bool
blockOf (const std::string &text, const std::string &device, thHidBlock &out,
         bool &found, std::string &err)
{
    found = false;
    if (text.find_first_not_of(" \t\r\n") == std::string::npos)
        return true;

    std::vector<thHidBlock> blocks;
    int line = 0;

    if (!thHidParseProfiles(text, blocks, err, line))
    {
        err = "your profile doesn't read (line " + std::to_string(line) +
              ": " + err + "), so it can't be edited here";
        return false;
    }
    for (const thHidBlock &b : blocks)
    {
        if (b.device != device)
        {
            err = "your profile is for '" + b.device + "', not '" + device + "'";
            return false;
        }
        if (b.hand.empty())
        {
            out = b;
            found = true;
        }
    }
    return true;
}

/* The indentation of the block's statements, or four spaces for one with
   none. */
std::string
indentOf (const std::string &text, const thHidBlock &blk)
{
    size_t at = std::string::npos;

    for (const thHidBinding &b : blk.bindings)
        at = std::min(at, b.from);
    for (const auto &i : blk.ignoreAt)
        at = std::min(at, i.first);
    if (at == std::string::npos)
        return "    ";

    const size_t ls = thLineStart(text, at);

    return text.find_first_not_of(" \t", ls) == at ? text.substr(ls, at - ls)
                                                   : "    ";
}

/* `stmt' added to the player's block for either hand, made if there is
   none. */
void
append (std::string &text, const std::string &device, const thHidBlock *blk,
        const std::string &stmt, std::vector<thTextEdit> &edits)
{
    if (blk == NULL)
    {
        const bool nl = text.empty() || text.back() == '\n';

        edits.push_back({ text.size(), text.size(),
                          std::string(nl ? "" : "\n") + "device " + device +
                          " {\n    " + stmt + "\n};\n" });
        return;
    }

    bool ownLine;
    const size_t at = thEndOfBlock(text, blk->close, ownLine);

    edits.push_back(ownLine
        ? thTextEdit{ at, at, indentOf(text, *blk) + stmt + "\n" }
        : thTextEdit{ at, at, thGapBefore(text, at) + stmt + " " });
}

/* The player's bindings of `control' in the block, in order. */
std::vector<const thHidBinding *>
boundTo (const thHidBlock &blk, const std::string &control)
{
    std::vector<const thHidBinding *> out;

    for (const thHidBinding &b : blk.bindings)
        if (b.control == control)
            out.push_back(&b);
    return out;
}

/* The edits made, if what they make still reads: a second binding of a
   button, say, doesn't, and the text is left as it was. */
bool
apply (std::string &text, std::vector<thTextEdit> &edits, std::string &err)
{
    std::string edited = text;
    std::vector<thHidBlock> blocks;
    int line = 0;

    if (!thApplyEdits(edited, edits))
        return false;
    if (!thHidParseProfiles(edited, blocks, err, line))
    {
        err = "that wouldn't read (line " + std::to_string(line) + ": " + err + ")";
        return false;
    }
    text = edited;
    return true;
}

}

bool
thhTakes (thHidBinding::Kind kind, const std::string &option)
{
    using K = thHidBinding;
    static const struct { const char *option; std::vector<K::Kind> kinds; } TAKES[] = {
        { "channel", { K::NOTE, K::VALUE, K::STRIKE, K::LOOP } },
        { "min", { K::VALUE, K::VELOCITY, K::STRIKE, K::AUX } },
        { "max", { K::VALUE, K::VELOCITY, K::STRIKE, K::AUX } },
        { "at", { K::STRIKE, K::LOOP } },
        { "off", { K::STRIKE, K::LOOP } },
        { "step", { K::PITCH } },
        { "scale", { K::PITCH, K::BEND } },
        { "range", { K::PITCH, K::BEND } },
        { "center", { K::VALUE, K::PITCH, K::BEND, K::AUX } },
        { "smooth", { K::VALUE, K::BEND, K::AUX } },
        { "shape", { K::VALUE } },
        { "both", { K::BEND, K::AUX } },
        { "latch", { K::NOTE } },
    };

    for (const auto &t : TAKES)
        if (option == t.option)
            return std::find(t.kinds.begin(), t.kinds.end(), kind) != t.kinds.end();
    return false;
}

std::string
thhBindingText (const thHidBinding &b)
{
    std::string action, opts;
    const auto opt = [&](const std::string &k, const std::string &v)
    {
        opts += " " + k + " = " + v + ";";
    };

    switch (b.kind)
    {
    case thHidBinding::NOTE:
        action = "note " + std::to_string(b.semis);
        break;
    case thHidBinding::HOLD:
        action = "hold " + b.layer;
        break;
    case thHidBinding::TOGGLE:
        action = "toggle " + b.layer;
        break;
    case thHidBinding::OCTAVE:
        action = "octave " + std::to_string(b.semis);
        break;
    case thHidBinding::VALUE:
        action = "\"" + b.arg + "\"";
        break;
    case thHidBinding::PITCH:
        action = "pitch";
        break;
    case thHidBinding::VELOCITY:
        action = "velocity";
        break;
    case thHidBinding::STRIKE:
        action = "strike " + std::to_string(b.semis);
        break;
    case thHidBinding::BEND:
        action = "bend";
        break;
    case thHidBinding::AUX:
        action = "aux " + std::to_string(b.semis);
        break;
    case thHidBinding::LOOP:
        action = "loop " + b.arg;
        break;
    }

    const std::string &word = b.kind == thHidBinding::LOOP ? b.arg : b.layer;

    if (!reads(b.control, thWordToken::WORD, b.control) ||
        (b.kind == thHidBinding::VALUE &&
         !reads(action, thWordToken::STRING, b.arg)) ||
        (!word.empty() && !reads(word, thWordToken::WORD, word)))
        return "";

    /* An option a binding kept from another kind isn't written. */
    const auto is = [&](const char *option) { return thhTakes(b.kind, option); };

    if (b.channel != 0 && is("channel"))
        opt("channel", std::to_string(b.channel));
    if (b.hasMin && is("min"))
        opt("min", number(b.min));
    if (b.hasMax && is("max"))
        opt("max", number(b.max));
    /* At and off as the parser has them where they aren't given. */
    if (b.at != 0.6 && is("at"))
        opt("at", number(b.at));
    if (b.off != std::max(0.0, b.at - 0.2) && is("off"))
        opt("off", number(b.off));
    if (b.octaves && is("step"))
        opt("step", "octave");
    if (!b.scale.empty() && is("scale"))
    {
        const char *scale = thHidScaleName(b.scale);

        if (scale == NULL)
            return "";
        opt("scale", scale);
    }
    if (is("range"))
        opt("range", std::to_string(b.range));
    if (b.center != 0 && is("center"))
        opt("center", number(b.center));
    if (b.smooth != 0 && is("smooth"))
        opt("smooth", number(b.smooth));
    if (b.shape == thHidBinding::EXP && is("shape"))
        opt("shape", "exp");
    if (b.both && is("both"))
        opt("of", "both");
    if (b.latch && is("latch"))
        opts += " latch;";

    return b.control + " = " + action + (opts.empty() ? "" : " {" + opts + " }") +
           ";";
}

bool
thhProfileSet (std::string &text, const std::string &device,
               const thHidBinding &b, int which, std::string &err)
{
    thHidBlock blk;
    bool found;

    if (!blockOf(text, device, blk, found, err))
        return false;

    const std::string stmt = thhBindingText(b);

    if (stmt.empty())
    {
        err = "that binding has a value the profile can't write";
        return false;
    }

    const auto bound = found ? boundTo(blk, b.control)
                             : std::vector<const thHidBinding *>();
    std::vector<thTextEdit> edits;

    if (which >= 0 && which < (int)bound.size())
        edits.push_back({ bound[which]->from, bound[which]->to, stmt });
    else
        append(text, device, found ? &blk : NULL, stmt, edits);
    return apply(text, edits, err);
}

bool
thhProfileRemove (std::string &text, const std::string &device,
                  const std::string &control, int which, std::string &err)
{
    thHidBlock blk;
    bool found;

    if (!blockOf(text, device, blk, found, err))
        return false;

    const auto bound = found ? boundTo(blk, control)
                             : std::vector<const thHidBinding *>();

    if (which < 0 || which >= (int)bound.size())
        return true;

    std::vector<thTextEdit> edits = {
        thEraseStmt(text, bound[which]->from, bound[which]->to) };

    return apply(text, edits, err);
}

bool
thhProfileReset (std::string &text, const std::string &device,
                 const std::string &control, std::string &err)
{
    thHidBlock blk;
    bool found;

    if (!blockOf(text, device, blk, found, err))
        return false;
    if (!found)
        return true;

    std::vector<thTextEdit> edits;

    for (const thHidBinding *b : boundTo(blk, control))
        edits.push_back(thEraseStmt(text, b->from, b->to));
    for (size_t i = 0; i < blk.ignore.size(); i++)
        if (blk.ignore[i].first == control)
            edits.push_back(thEraseStmt(text, blk.ignoreAt[i].first,
                                        blk.ignoreAt[i].second));
    return apply(text, edits, err);
}

bool
thhProfileOff (std::string &text, const std::string &device,
               const std::string &control, std::string &err)
{
    if (!thhProfileReset(text, device, control, err))
        return false;

    thHidBlock blk;
    bool found;
    std::vector<thTextEdit> edits;

    if (!blockOf(text, device, blk, found, err))
        return false;
    append(text, device, found ? &blk : NULL, "ignore " + control + ";", edits);
    return apply(text, edits, err);
}
