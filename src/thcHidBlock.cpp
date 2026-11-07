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

#include "config.h"

#include <algorithm>
#include <cmath>

#include "think.h"
#include "thcHidBlock.h"

typedef thcGenToken Tok;

namespace {

class Parser
{
public:
    Parser (const std::vector<Tok> &t, size_t &i, std::string &err,
            int &errLine)
        : t_(t), i_(i), err_(err), errLine_(errLine) {}

    bool block (thcHidBlock &out);
    bool profile (thcHidBlock &out);
    bool profiles (std::vector<thcHidBlock> &out);

private:
    const std::vector<Tok> &t_;
    size_t &i_;
    std::string &err_;
    int &errLine_;

    const Tok &peek () const { return t_[i_]; }

    /* Whether a modifier's layer must be in the same block: so for a block
       read alone, and not for a profile of blocks, overlaid on another
       profile's, where it may be in any of them. */
    bool ownLayers_ = true;

    bool fail (int line, const std::string &msg)
    {
        err_ = msg;
        errLine_ = line;
        return false;
    }

    bool fail (const Tok &at, const std::string &msg)
    {
        /* END has no line of its own worth naming; the token before it does. */
        return fail((at.kind == Tok::END && i_ > 0) ? t_[i_ - 1].line : at.line,
                    msg);
    }

    bool isPunct (const Tok &tok, char c) const
    {
        return tok.kind == Tok::PUNCT && tok.text.size() == 1 &&
               tok.text[0] == c;
    }

    bool isWord (const Tok &tok, const char *w) const
    {
        return tok.kind == Tok::WORD && tok.text == w;
    }

    bool expect (char c)
    {
        if (!isPunct(peek(), c))
            return fail(peek(), std::string("expected '") + c + "'");
        i_++;
        return true;
    }

    /* True at the `}' that ends a body, having stepped past it, and at the
       end of the file, having refused it: callers then check err_. */
    bool closes ()
    {
        if (peek().kind == Tok::END)
            return !fail(peek(), "expected '}'");
        if (!isPunct(peek(), '}'))
            return false;
        i_++;
        return true;
    }

    bool word (std::string &out, const std::string &what);
    bool number (double &out, const std::string &what);
    bool integer (int &out, int lo, int hi, const std::string &what);
    bool baseNote (int &out);
    bool statement (thcHidBlock &blk, std::vector<thcHidBinding> &bindings,
                    int &channel, int &base, bool inLayer);
    bool layer (thcHidBlock &blk);
    bool when (thcHidLayer &l);
    bool binding (thcHidBinding &out, bool inLayer);
    bool options (thcHidBinding &b);
};

bool
Parser::word (std::string &out, const std::string &what)
{
    if (peek().kind != Tok::WORD)
        return fail(peek(), what + " wants a name");
    out = peek().text;
    i_++;
    return true;
}

/* A `-' right against a number after a word reaches here as two tokens (the
   tokenizer's endsAValue), so it is rejoined here. With a space between, it
   is not a sign, and nor is a second one. */
bool
Parser::number (double &out, const std::string &what)
{
    double sign = 1;

    if (isPunct(peek(), '-') && t_[i_ + 1].kind == Tok::NUMBER &&
        t_[i_ + 1].off != peek().end)
        return fail(peek(), "a sign goes against its number, with no space: -5, not - 5");
    if (isPunct(peek(), '-') && t_[i_ + 1].kind == Tok::NUMBER &&
        t_[i_ + 1].off == peek().end && t_[i_ + 1].text[0] != '-')
    {
        sign = -1;
        i_++;
    }

    if (peek().kind != Tok::NUMBER)
        return fail(peek(), what + " wants a number");
    if (!std::isfinite(peek().num))
        return fail(peek(), what + " is out of range");

    out = sign * peek().num;
    i_++;
    return true;
}

bool
Parser::integer (int &out, int lo, int hi, const std::string &what)
{
    const Tok &at = peek();
    double v;

    if (!number(v, what))
        return false;
    if (v != std::floor(v))
        return fail(at, what + " wants a whole number");
    if (v < lo || v > hi)
        return fail(at, what + " is " + std::to_string(lo) + " to " +
                    std::to_string(hi));

    out = (int)v;
    return true;
}

bool
Parser::baseNote (int &out)
{
    const Tok &at = peek();

    if (at.kind != Tok::STRING)
        return fail(at, "write the base note in quotes, as \"C4\": unquoted, "
                    "a # would start a comment");

    std::vector<int> notes;
    std::string bad;

    if (!thcGenLoader::parseNoteList(at.text, notes, bad))
        return fail(at, "'" + bad + "' is not a note");
    if (notes.size() != 1 || notes[0] < 0)
        return fail(at, "the base is one note");

    out = notes[0];
    i_++;
    return true;
}

/* The rule .gen applies to a sink's chanarg (thcGenFile.cpp): a letter, then
   letters, digits and underscores, optionally behind the effect's prefix.
   ASCII by hand, as there, since <cctype> answers by locale. */
bool
chanargName (const std::string &name)
{
    const std::string prefix = TH_EFFECT_PREFIX;
    const size_t from = name.compare(0, prefix.size(), prefix) == 0
                        ? prefix.size() : 0;

    for (size_t i = from; i < name.size(); i++)
    {
        const char c = name[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

        if (!letter && (i == from || !((c >= '0' && c <= '9') || c == '_')))
            return false;
    }

    return name.size() > from;
}

/* `{ key = value; ... }' after an action. Which keys an action takes is
   decided here, so a key it doesn't take is refused by name. */
bool
Parser::options (thcHidBinding &b)
{
    const bool value = b.kind == thcHidBinding::VALUE;
    const bool strike = b.kind == thcHidBinding::STRIKE;
    const bool vel = b.kind == thcHidBinding::VELOCITY || strike;
    const bool pitch = b.kind == thcHidBinding::PITCH;
    const bool bend = b.kind == thcHidBinding::BEND;
    const bool aux = b.kind == thcHidBinding::AUX;

    if (!isPunct(peek(), '{'))
        return true;
    i_++;

    while (!closes())
    {
        const Tok &key = peek();
        const std::string &k = key.text;

        if (key.kind != Tok::WORD)
            return fail(key, "expected a setting");
        i_++;

        /* The one setting that is a word alone. */
        if (k == "latch" && b.kind != thcHidBinding::NOTE)
            return fail(key, "'latch' is a note's setting");
        if (k == "latch")
        {
            b.latch = true;
            if (!expect(';'))
                return false;
            continue;
        }

        if (!expect('='))
            return false;

        if (k == "channel" && (b.kind == thcHidBinding::NOTE || value || strike))
        {
            if (!integer(b.channel, 1, 16, "channel"))
                return false;
        }
        else if ((k == "min" || k == "max") && (value || vel || aux))
        {
            double v;
            int iv;

            if (value || aux ? !number(v, k) : !integer(iv, 1, 127, "velocity"))
                return false;
            (k == "min" ? b.min : b.max) = value || aux ? v : iv;
            (k == "min" ? b.hasMin : b.hasMax) = true;
        }
        else if ((k == "at" || k == "off") && strike)
        {
            double v;

            if (!number(v, k))
                return false;
            if (v < 0 || v > 1)
                return fail(t_[i_ - 1], k + " is 0 to 1");
            (k == "at" ? b.at : b.off) = v;
        }
        else if (k == "of" && (bend || aux))
        {
            if (!isWord(peek(), "both"))
                return fail(peek(), "'" + peek().text + "' is not whose notes; "
                            "write both");
            b.both = true;
            i_++;
        }
        else if (k == "smooth" && (value || bend || aux))
        {
            if (!number(b.smooth, k))
                return false;
            if (b.smooth < 0)
                return fail(t_[i_ - 1], "smooth is 0 or more seconds");
        }
        else if (k == "center" && (value || pitch || bend || aux))
        {
            if (!number(b.center, k))
                return false;
            if (b.center < -1 || b.center > 1)
                return fail(t_[i_ - 1], "center is -1 to 1");
        }
        else if (k == "shape" && value)
        {
            const Tok &v = peek();

            if (isWord(v, "linear"))
                b.shape = thcHidBinding::LINEAR;
            else if (isWord(v, "exp"))
                b.shape = thcHidBinding::EXP;
            else
                return fail(v, "'" + v.text + "' is not a shape; write "
                            "linear or exp");
            i_++;
        }
        else if (k == "step" && pitch)
        {
            const Tok &v = peek();

            if (isWord(v, "octave"))
                b.octaves = true;
            else if (isWord(v, "semitone"))
                b.octaves = false;
            else
                return fail(v, "'" + v.text + "' is not a step; write "
                            "octave or semitone");
            i_++;
        }
        else if (k == "scale" && pitch)
        {
            static const struct { const char *name; std::vector<int> steps; }
                scales[] = {
                    { "major", { 0, 2, 4, 5, 7, 9, 11 } },
                    { "minor", { 0, 2, 3, 5, 7, 8, 10 } },
                    { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
                    { "pentatonic", { 0, 2, 4, 7, 9 } },
                    { "minor_pentatonic", { 0, 3, 5, 7, 10 } },
                    { "blues", { 0, 3, 5, 6, 7, 10 } },
                };
            const Tok &v = peek();

            b.scale.clear();
            for (const auto &sc : scales)
                if (isWord(v, sc.name))
                    b.scale = sc.steps;
            if (b.scale.empty())
                return fail(v, "'" + v.text + "' is not a scale; write major, "
                            "minor, dorian, pentatonic, minor_pentatonic or "
                            "blues");
            i_++;
        }
        else if (k == "range" && (pitch || bend))
        {
            if (!integer(b.range, 1, 127, "range"))
                return false;
        }
        else
            return fail(key, "'" + k + "' is not a setting here");

        if (!expect(';'))
            return false;
    }
    if (!err_.empty())
        return false;

    if (vel && b.hasMin && b.hasMax && b.min > b.max)
        return fail(b.line, "the velocity's min is above its max");
    if (b.shape == thcHidBinding::EXP &&
        ((b.hasMin && b.min <= 0) || (b.hasMax && b.max <= 0)))
        return fail(b.line, "an exp shape needs a range above 0");
    if (b.octaves && !b.scale.empty())
        return fail(b.line, "a scale steps by its degrees, not by octaves");
    if ((pitch || bend) && b.range == 0)
        return fail(b.line, std::string(pitch ? "a pitch axis" : "a bend") +
                    " needs a range");

    return true;
}

bool
Parser::binding (thcHidBinding &b, bool inLayer)
{
    b.control = peek().text;
    b.line = peek().line;
    i_++;
    if (!expect('='))
        return false;

    const Tok &act = peek();

    if (act.kind == Tok::STRING)
    {
        if (!chanargName(act.text))
            return fail(act, "'" + act.text + "' is not a chanarg name");
        b.kind = thcHidBinding::VALUE;
        b.arg = act.text;
        i_++;
    }
    else if (isWord(act, "note"))
    {
        b.kind = thcHidBinding::NOTE;
        i_++;
        if (!integer(b.semis, -127, 127, "note"))
            return false;
    }
    else if (isWord(act, "octave"))
    {
        b.kind = thcHidBinding::OCTAVE;
        i_++;
        if (!integer(b.semis, -10, 10, "octave"))
            return false;
        if (b.semis == 0)
            return fail(t_[i_ - 1], "an octave step is not 0");
    }
    else if (isWord(act, "hold") || isWord(act, "toggle"))
    {
        if (inLayer)
            return fail(act, "a modifier can't sit inside a layer");
        b.kind = act.text == "hold" ? thcHidBinding::HOLD
                                    : thcHidBinding::TOGGLE;
        i_++;
        if (!word(b.layer, act.text))
            return false;
    }
    else if (isWord(act, "pitch"))
    {
        b.kind = thcHidBinding::PITCH;
        i_++;
        if (!isPunct(peek(), '{'))
            return fail(peek(), "a pitch axis needs a range");
    }
    else if (isWord(act, "velocity"))
    {
        b.kind = thcHidBinding::VELOCITY;
        i_++;
    }
    else if (isWord(act, "bend"))
    {
        b.kind = thcHidBinding::BEND;
        i_++;
        if (!isPunct(peek(), '{'))
            return fail(peek(), "a bend needs a range");
    }
    else if (isWord(act, "aux"))
    {
        b.kind = thcHidBinding::AUX;
        i_++;
        if (!integer(b.semis, 0, TH_NOTE_AUX - 1, "aux"))
            return false;
    }
    else if (isWord(act, "strike"))
    {
        b.kind = thcHidBinding::STRIKE;
        b.off = -1;     /* until it is given, or follows at */
        i_++;
        if (!integer(b.semis, -127, 127, "strike"))
            return false;
    }
    else
        return fail(act, "'" + act.text + "' is not an action; write note, "
                    "strike, hold, toggle, octave, pitch, velocity, bend, aux "
                    "or a quoted arg name");

    if (!options(b))
        return false;

    if (b.kind == thcHidBinding::STRIKE)
    {
        if (b.at <= 0)
            return fail(b.line, "a strike's at is above 0, where it rests");
        if (b.off < 0)
            b.off = std::max(0.0, b.at - 0.2);
        if (b.off >= b.at)
            return fail(b.line, "a strike turns off under where it turns on");
    }

    return expect(';');
}

/* One statement of a device's body or a layer's: a setting, a layer (in a
   device only) or a binding. */
bool
Parser::statement (thcHidBlock &blk, std::vector<thcHidBinding> &bindings,
                   int &channel, int &base, bool inLayer)
{
    const Tok &tok = peek();
    const std::string &k = tok.text;

    if (tok.kind != Tok::WORD)
        return fail(tok, "expected a control or a setting");

    if (inLayer && (k == "layer" || k == "velocity" || k == "ignore"))
        return fail(tok, k + " can't be set inside a layer");
    if (k == "ignore")
    {
        std::string name;

        i_++;
        if (!word(name, "ignore"))
            return false;
        blk.ignore.push_back(std::make_pair(name, tok.line));
        return expect(';');
    }
    if (k == "when")
        return fail(tok, "a posture's when belongs in a layer");
    if (k == "layer")
        return layer(blk);

    if (k == "channel" || k == "base" || k == "velocity")
    {
        i_++;
        if (!expect('='))
            return false;
        if (k == "channel" ? !integer(channel, 1, 16, "channel")
            : k == "base"  ? !baseNote(base)
            : !integer(blk.velocity, 1, 127, "velocity"))
            return false;
        if (!inLayer)
            (k == "channel" ? blk.hasChannel : k == "base" ? blk.hasBase
                                                           : blk.hasVelocity) = true;
        return expect(';');
    }

    thcHidBinding b;

    if (!binding(b, inLayer))
        return false;

    /* An axis may ride several knobs and pitch axes at once; anything else
       is one binding to a control. */
    const auto repeats = [](const thcHidBinding &x) {
        return x.kind == thcHidBinding::VALUE || x.kind == thcHidBinding::PITCH ||
               x.kind == thcHidBinding::BEND || x.kind == thcHidBinding::AUX;
    };

    for (size_t i = 0; i < bindings.size(); i++)
        if (bindings[i].control == b.control &&
            (!repeats(bindings[i]) || !repeats(b)))
            return fail(b.line, "'" + b.control + "' is already bound on line " +
                        std::to_string(bindings[i].line));

    bindings.push_back(b);
    return true;
}

/* `when { axis = at-least; ... };' */
bool
Parser::when (thcHidLayer &l)
{
    if (l.whenLine)
        return fail(peek(), "a layer has one when; put every axis in it");
    l.whenLine = peek().line;
    i_++;
    if (!expect('{'))
        return false;
    while (!closes())
    {
        std::string name;
        double v;

        if (!word(name, "when") || !expect('=') || !number(v, name))
            return false;
        if (v < -1 || v > 1)
            return fail(t_[i_ - 1], name + " is -1 to 1");
        l.when.push_back(std::make_pair(name, v));
        if (!expect(';'))
            return false;
    }
    return err_.empty() && expect(';');
}

bool
Parser::layer (thcHidBlock &blk)
{
    thcHidLayer l;

    l.line = peek().line;
    i_++;
    if (!word(l.name, "layer"))
        return false;

    for (size_t i = 0; i < blk.layers.size(); i++)
        if (blk.layers[i].name == l.name)
            return fail(t_[i_ - 1], "layer '" + l.name + "' is already "
                        "declared on line " +
                        std::to_string(blk.layers[i].line));

    if (!expect('{'))
        return false;
    while (!closes())
        if (isWord(peek(), "when") ? !when(l)
            : !statement(blk, l.bindings, l.channel, l.base, true))
            return false;
    if (!err_.empty() || !expect(';'))
        return false;

    blk.layers.push_back(l);
    return true;
}

bool
Parser::block (thcHidBlock &out)
{
    if (!expect('{'))
        return false;
    while (!closes())
        if (!statement(out, out.bindings, out.channel, out.base, false))
            return false;
    if (!err_.empty())
        return false;

    /* Layers may be named before they are declared, so this waits until
       the whole block is read. */
    for (size_t i = 0; ownLayers_ && i < out.bindings.size(); i++)
    {
        const thcHidBinding &b = out.bindings[i];
        bool found = false;

        if (b.kind != thcHidBinding::HOLD && b.kind != thcHidBinding::TOGGLE)
            continue;

        for (size_t j = 0; j < out.layers.size(); j++)
            found = found || out.layers[j].name == b.layer;

        if (!found)
            return fail(b.line, "there is no layer '" + b.layer + "'");
    }

    return true;
}

bool
Parser::profile (thcHidBlock &out)
{
    if (!isWord(peek(), "device"))
        return fail(peek(), "a profile starts with 'device'");
    out.line = peek().line;
    i_++;
    if (!word(out.device, "device") || !block(out) || !expect(';'))
        return false;
    if (peek().kind != Tok::END)
        return fail(peek(), "a profile holds one device");
    return true;
}

/* `device NAME [left|right] { ... };', again for each hand. */
bool
Parser::profiles (std::vector<thcHidBlock> &out)
{
    out.clear();
    ownLayers_ = false;
    while (peek().kind != Tok::END)
    {
        thcHidBlock b;

        if (!isWord(peek(), "device"))
            return fail(peek(), "a profile is 'device' blocks");
        b.line = peek().line;
        i_++;
        if (!word(b.device, "device"))
            return false;
        if (!out.empty() && b.device != out[0].device)
            return fail(t_[i_ - 1], "a profile is of one device");

        if (peek().kind == Tok::WORD)
        {
            if (peek().text != "left" && peek().text != "right")
                return fail(peek(), "'" + peek().text + "' is not a hand; "
                            "write left or right");
            b.hand = peek().text;
            i_++;
        }
        for (size_t k = 0; k < out.size(); k++)
            if (out[k].hand == b.hand)
                return fail(t_[i_ - 1], (b.hand.empty() ? "either hand"
                                                         : "the " + b.hand +
                                                           " hand") +
                            " already has a block");

        if (!block(b) || !expect(';'))
            return false;
        out.push_back(b);
    }
    return true;
}

} /* namespace */

bool
thcHidParseBlock (const std::vector<Tok> &toks, size_t &pos, thcHidBlock &out,
                  std::string &err, int &errLine)
{
    err.clear();
    return Parser(toks, pos, err, errLine).block(out);
}

bool
thcHidParseProfile (const std::string &text, thcHidBlock &out,
                    std::string &err, int &errLine)
{
    std::vector<Tok> toks;
    size_t pos = 0;

    out = thcHidBlock();
    if (!thcGenLoader::tokenize(text, toks, err, errLine))
        return false;

    return Parser(toks, pos, err, errLine).profile(out);
}

bool
thcHidParseProfiles (const std::string &text, std::vector<thcHidBlock> &out,
                     std::string &err, int &errLine)
{
    std::vector<Tok> toks;
    size_t pos = 0;

    out.clear();
    if (!thcGenLoader::tokenize(text, toks, err, errLine))
        return false;

    return Parser(toks, pos, err, errLine).profiles(out);
}
