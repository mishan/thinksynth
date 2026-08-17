/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include "config.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "think.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"

thcGenLoader::thcGenLoader (const std::map<std::string, thcPlugin *> &plugins)
    : plugins_(plugins), pos_(0), hasSeed_(false), seed_(0)
{
}

void
thcGenLoader::error (int line, const std::string &msg)
{
    std::ostringstream s;

    s << path_ << ":" << line << ": " << msg;
    errors_.push_back(s.str());
}

/* ---- pitch names ------------------------------------------------------ */

/* "F3 Ab3 C4" or "F3,Ab3,C4" -> {53, 56, 60}. The one place in the tree
 * that turns pitch text into numbers; plugins receive only the numbers. */
bool
thcGenLoader::parseNoteList (const std::string &text, std::vector<int> &out,
                             std::string &bad)
{
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };  /* A..G */

    out.clear();

    size_t i = 0;

    while (i < text.size())
    {
        if (text[i] == ' ' || text[i] == ',' || text[i] == '\t')
        {
            i++;
            continue;
        }

        size_t start = i;
        char letter = text[i];

        if (letter < 'A' || letter > 'G')
        {
            bad = text.substr(start, text.find_first_of(" ,\t", start) - start);
            return false;
        }

        int n = semis[letter - 'A'];

        i++;

        if (i < text.size() && text[i] == '#') { n++; i++; }
        else if (i < text.size() && text[i] == 'b') { n--; i++; }

        bool neg = false;

        if (i < text.size() && text[i] == '-') { neg = true; i++; }

        if (i >= text.size() || text[i] < '0' || text[i] > '9')
        {
            bad = text.substr(start, i - start + 1);
            return false;
        }

        int octave = 0;

        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            octave = octave * 10 + (text[i++] - '0');

        if (neg)
            octave = -octave;

        int midi = 12 * (octave + 1) + n;     /* C4 == 60 */

        if (midi < 0 || midi > 127)
        {
            bad = text.substr(start, i - start);
            return false;
        }

        out.push_back(midi);
    }

    return !out.empty();
}

static std::string
noteListToString (const std::vector<int> &notes)
{
    std::ostringstream s;

    for (size_t i = 0; i < notes.size(); i++)
    {
        if (i)
            s << ",";
        s << notes[i];
    }

    return s.str();
}

/* ---- lexing ----------------------------------------------------------- */

/* The .dsp lexical layer, reproduced: `#' comments to end of line,
 * whitespace, numbers (a leading `-' is folded in here -- .gen has no
 * arithmetic for a grammar to hang SUB on), words, quoted strings with
 * no escapes and no embedded newline, `::', and the punctuation.
 *
 * Every token carries its byte span, and a STRING's span includes its
 * quotes: thcGenEdit replaces spans, and what sits between them --
 * comments, indentation, the author's blank lines -- is never touched. */
bool
thcGenLoader::tokenize (const std::string &text, std::vector<thcGenToken> &out,
                        std::string &err, int &errLine)
{
    int line = 1;
    size_t i = 0;
    bool ok = true;

    /* A public tokenizer owns its output: a caller reusing one vector
       across files must not get the previous file's tail. */
    out.clear();
    err.clear();
    errLine = 0;

    while (i < text.size())
    {
        char c = text[i];

        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        if (c == '#')
        {
            while (i < text.size() && text[i] != '\n')
                i++;
            continue;
        }

        Token t;

        t.line = line;
        t.num = 0;
        t.off = i;

        if ((c >= '0' && c <= '9') ||
            (c == '-' && i + 1 < text.size() &&
             text[i + 1] >= '0' && text[i + 1] <= '9'))
        {
            size_t start = i;

            if (c == '-')
                i++;
            while (i < text.size() &&
                   ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
                i++;

            t.kind = Token::NUMBER;
            t.text = text.substr(start, i - start);

            /* strtod with its end pointer, not atof: the scan above is
               permissive about dots, and "1..2" quietly becoming 1.0
               is the opposite of a validated literal. */
            char *end = NULL;

            t.num = strtod(t.text.c_str(), &end);

            if (end == NULL || *end != 0)
            {
                err = "'" + t.text + "' is not a number";
                errLine = line;
                ok = false;
                break;
            }

            t.end = i;
            out.push_back(t);
            continue;
        }

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        {
            size_t start = i;

            while (i < text.size() &&
                   ((text[i] >= 'a' && text[i] <= 'z') ||
                    (text[i] >= 'A' && text[i] <= 'Z') ||
                    (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
                i++;

            t.kind = Token::WORD;
            t.text = text.substr(start, i - start);
            t.end = i;
            out.push_back(t);
            continue;
        }

        if (c == '"')
        {
            size_t start = ++i;

            while (i < text.size() && text[i] != '"' && text[i] != '\n')
                i++;

            if (i >= text.size() || text[i] != '"')
            {
                err = "unterminated string";
                errLine = line;
                ok = false;
                break;
            }

            t.kind = Token::STRING;
            t.text = text.substr(start, i - start);
            i++;
            t.end = i;              /* both quotes inside the span       */
            out.push_back(t);
            continue;
        }

        if (c == ':' && i + 1 < text.size() && text[i + 1] == ':')
        {
            t.kind = Token::MODSEP;
            t.text = "::";
            i += 2;
            t.end = i;
            out.push_back(t);
            continue;
        }

        if (c == '@')
        {
            size_t start = ++i;

            while (i < text.size() &&
                   ((text[i] >= 'a' && text[i] <= 'z') ||
                    (text[i] >= 'A' && text[i] <= 'Z') ||
                    (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
                i++;

            if (i == start)
            {
                err = "'@' with no knob name after it";
                errLine = line;
                ok = false;
                break;
            }

            t.kind = Token::KNOB;
            t.text = text.substr(start, i - start);
            t.end = i;              /* the '@' inside the span           */
            out.push_back(t);
            continue;
        }

        if (c == ';' || c == '=' || c == '{' || c == '}' || c == '.')
        {
            t.kind = Token::PUNCT;
            t.text = std::string(1, c);
            i++;
            t.end = i;
            out.push_back(t);
            continue;
        }

        {
            std::ostringstream s;

            s << "stray character '" << c << "'";
            err = s.str();
            errLine = line;
        }
        ok = false;
        break;
    }

    Token end;

    end.kind = Token::END;
    end.line = line;
    end.num = 0;
    end.off = end.end = text.size();
    out.push_back(end);

    return ok;
}

bool
thcGenLoader::lex (const std::string &path, std::vector<Token> &out)
{
    /* Binary for the same reason thcGenEdit reads binary: the tokens
       carry byte offsets into the file as it sits on disk, and text
       mode on Windows would shift every offset by one per line. */
    std::ifstream in(path.c_str(), std::ios::binary);

    if (!in)
    {
        error(0, "cannot open file");
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    std::string err;
    int errLine = 0;

    if (!tokenize(text, out, err, errLine))
    {
        error(errLine, err);
        return false;
    }

    return true;
}

/* 60 -> "C4". Flats on the black keys -- Ab3, not G#3 -- because that is
 * how the shipped piece spells them, and a writer that changes the
 * spelling of a pitch nobody edited has edited it anyway. */
std::string
thcGenLoader::noteName (int midi)
{
    static const char *names[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };

    if (midi < 0 || midi > 127)
        return "";

    std::ostringstream s;

    s << names[midi % 12] << (midi / 12 - 1);

    return s.str();
}

/* ---- token stream helpers --------------------------------------------- */

const thcGenLoader::Token &
thcGenLoader::peek (void) const
{
    return tokens_[pos_ < tokens_.size() ? pos_ : tokens_.size() - 1];
}

thcGenLoader::Token
thcGenLoader::take (void)
{
    Token t = peek();

    if (pos_ < tokens_.size() - 1)
        pos_++;

    return t;
}

bool
thcGenLoader::expectPunct (char c)
{
    const Token &t = peek();

    if (t.kind == Token::PUNCT && t.text[0] == c)
    {
        take();
        return true;
    }

    std::ostringstream s;

    s << "expected '" << c << "'";

    if (t.kind == Token::END)
        s << " before end of file";
    else
        s << " before '" << t.text << "'";

    error(t.line, s.str());
    return false;
}

/* One mistake should read as one error, not as fifty knock-ons: skip to
 * the `;' that ends the statement, tracking block depth so a mistake
 * inside a chain body skips the body, not half the file. */
void
thcGenLoader::skipStatement (void)
{
    int depth = 0;

    while (peek().kind != Token::END)
    {
        Token t = take();

        if (t.kind == Token::PUNCT)
        {
            if (t.text[0] == '{')
                depth++;
            else if (t.text[0] == '}' && depth > 0)
                depth--;
            else if (t.text[0] == ';' && depth == 0)
                return;
        }
    }
}

/* ---- the grammar ------------------------------------------------------ */

bool
thcGenLoader::load (const std::string &path, thcScheduler *sched)
{
    path_ = path;
    errors_.clear();
    tokens_.clear();
    scales_.clear();
    pos_ = 0;
    name_.clear();
    author_.clear();
    description_.clear();
    hasSeed_ = false;
    seed_ = 0;

    sched->stop();
    sched->clearChains();

    if (!lex(path, tokens_))
        return false;

    while (peek().kind != Token::END)
        if (!parseStatement(sched))
            skipStatement();

    if (!errors_.empty())
    {
        /* A file with any error loads nothing. Half a piece that plays
           is worse than a piece that says why it will not. */
        sched->clearChains();
        return false;
    }

    return true;
}

bool
thcGenLoader::parseStatement (thcScheduler *sched)
{
    const Token &t = peek();

    if (t.kind == Token::KNOB)
        return parseKnobStatement(sched);

    if (t.kind != Token::WORD)
    {
        error(t.line, "expected a statement, got '" + t.text + "'");
        return false;
    }

    if (t.text == "name" || t.text == "author" || t.text == "description")
    {
        Token key = take();
        const Token &v = peek();

        if (v.kind != Token::STRING)
        {
            error(v.line, key.text + " wants a quoted string");
            return false;
        }

        Token s = take();

        if (key.text == "name")
            name_ = s.text;
        else if (key.text == "author")
            author_ = s.text;
        else
            description_ = s.text;

        return expectPunct(';');
    }

    if (t.text == "tempo" || t.text == "seed")
    {
        Token key = take();
        const Token &v = peek();

        if (v.kind != Token::NUMBER)
        {
            error(v.line, key.text + " wants a number");
            return false;
        }

        Token n = take();

        if (key.text == "tempo")
            sched->setTempo(n.num);
        else
        {
            /* Stage seeds derive from the master seed at addStage time,
               so a seed arriving after any chain exists could not mean
               what it says. The writer's rules put it at the top; a
               hand-written file that does not gets told, not humored. */
            if (sched->chainCount() > 0)
            {
                error(n.line, "seed must come before the first chain");
                return false;
            }

            /* The whole point of a pinned seed is that this exact
               number replays this exact piece; -3 or 19.5 silently
               folded through an unsigned cast would replay something,
               just not what the file says. */
            if (n.num < 0 || n.num > 4294967295.0 ||
                n.num != std::floor(n.num))
            {
                error(n.line, "seed is a whole number, 0 to 4294967295");
                return false;
            }

            hasSeed_ = true;
            seed_ = (unsigned)n.num;
            sched->setMasterSeed(seed_);
        }

        return expectPunct(';');
    }

    if (t.text == "scale")
    {
        take();
        return parseScale();
    }

    if (t.text == "chain")
    {
        take();
        return parseChain(sched);
    }

    error(t.line, "unknown statement '" + t.text + "'");
    return false;
}

/* @density = 0.85;  and  @density.min = 0;  -- the same spellings the
 * .dsp parser gives a chanarg, applied to a piece knob (which IS a
 * thArg, so the metadata means what it already means everywhere). */
bool
thcGenLoader::parseKnobStatement (thcScheduler *sched)
{
    Token knobTok = take();          /* the @name */
    const std::string &kname = knobTok.text;

    if (peek().kind == Token::PUNCT && peek().text[0] == '=')
    {
        take();

        const Token &v = peek();

        if (v.kind != Token::NUMBER)
        {
            error(v.line, "@" + kname + " wants a number");
            return false;
        }

        Token n = take();

        sched->addKnob(kname, (float)n.num);

        return expectPunct(';');
    }

    if (peek().kind == Token::PUNCT && peek().text[0] == '.')
    {
        take();

        const Token &m = peek();

        if (m.kind != Token::WORD)
        {
            error(m.line, "expected a metadata name after '@" + kname + ".'");
            return false;
        }

        Token meta = take();

        if (!expectPunct('='))
            return false;

        thArg *knob = sched->knob(kname);

        if (knob == NULL)
        {
            error(meta.line, "'@" + kname + "." + meta.text +
                  "' set before '@" + kname + "' was declared");
            return false;
        }

        const Token &v = peek();

        if (v.kind == Token::NUMBER)
        {
            Token n = take();

            if (meta.text == "min")
                knob->setMin((float)n.num);
            else if (meta.text == "max")
                knob->setMax((float)n.num);
            else if (meta.text == "widget")
                knob->setWidgetType((thArg::WidgetType)(int)n.num);
            else if (meta.text == "step")
                knob->setStep((float)n.num, true);
            else
            {
                error(n.line, "unknown numeric knob metadata '" +
                      meta.text + "'");
                return false;
            }
        }
        else if (v.kind == Token::STRING)
        {
            Token s = take();

            if (meta.text == "label")
                knob->setLabel(s.text);
            else if (meta.text == "units")
                knob->setUnits(s.text);
            else if (meta.text == "group")
                knob->setGroup(s.text);
            else if (meta.text == "values")
                knob->setValueNames(s.text, true);
            else
            {
                error(s.line, "unknown string knob metadata '" +
                      meta.text + "'");
                return false;
            }
        }
        else
        {
            error(v.line, "knob metadata wants a number or a string");
            return false;
        }

        return expectPunct(';');
    }

    error(peek().line, "expected '=' or '.' after '@" + kname + "'");
    return false;
}

bool
thcGenLoader::parseScale (void)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "scale wants a name");
        return false;
    }

    Token nameTok = take();
    const Token &v = peek();

    if (v.kind != Token::STRING)
    {
        error(v.line, "scale " + nameTok.text + " wants a quoted note list");
        return false;
    }

    Token notes = take();

    std::vector<int> resolved;
    std::string bad;

    if (!parseNoteList(notes.text, resolved, bad))
    {
        error(notes.line, "scale " + nameTok.text +
              ": bad note name '" + bad + "'");
        return false;
    }

    scales_[nameTok.text] = resolved;

    return expectPunct(';');
}

bool
thcGenLoader::parseChain (thcScheduler *sched)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "chain wants a name");
        return false;
    }

    Token nameTok = take();

    if (!expectPunct('{'))
        return false;

    size_t chain = sched->addChain(nameTok.text);
    bool sawSink = false;
    bool sawGenerator = false;
    bool sawInput = false;
    bool ok = true;

    while (true)
    {
        const Token &t = peek();

        if (t.kind == Token::PUNCT && t.text[0] == '}')
        {
            take();
            break;
        }

        if (t.kind == Token::END)
        {
            error(t.line, "chain " + nameTok.text + ": unterminated body");
            return false;
        }

        if (t.kind != Token::WORD)
        {
            error(t.line, "chain " + nameTok.text +
                  ": expected input, stage or sink");
            return false;
        }

        if (t.text == "input")
        {
            take();

            const Token &w = peek();

            if (w.kind != Token::WORD || w.text != "midi")
            {
                error(w.line, "the only input there is is 'input midi'");
                return false;
            }

            take();

            if (!expectPunct(';'))
                return false;

            sched->setChainInput(chain, true);
            sawInput = true;
            continue;
        }

        if (t.text == "stage")
        {
            take();

            /* Textual order IS execution order, and sinks end a chain;
               a stage after a sink would execute somewhere the file
               does not say. */
            if (sawSink)
            {
                error(t.line, "chain " + nameTok.text +
                      ": stage after sink (sinks come last)");
                return false;
            }

            if (!parseStageBlock(sched, chain, nameTok.text))
                ok = false;
            else
            {
                thcChain *c = sched->chain(chain);

                /* The placement's role, not the module's capability: a
                   dual-entry plugin placed as xform:: does not tick,
                   and must not satisfy "this chain has a clock". */
                if (c != NULL && !c->stages.empty() &&
                    c->stages.back()->ticks)
                    sawGenerator = true;
            }
            continue;
        }

        if (t.text == "sink")
        {
            take();

            if (!parseSinkBlock(sched, chain))
                ok = false;
            else
                sawSink = true;
            continue;
        }

        error(t.line, "chain " + nameTok.text + ": unknown item '" +
              t.text + "'");
        return false;
    }

    if (!expectPunct(';'))
        ok = false;

    if (!sawSink)
    {
        error(nameTok.line, "chain " + nameTok.text + " has no sink");
        ok = false;
    }

    if (!sawGenerator && !sawInput)
    {
        error(nameTok.line, "chain " + nameTok.text +
              " has no generator stage and no 'input midi' -- "
              "nothing will ever flow through it");
        ok = false;
    }

    return ok;
}

bool
thcGenLoader::parseStageBlock (thcScheduler *sched, size_t chain,
                               const std::string &chainName)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "stage wants a name");
        return false;
    }

    Token stageName = take();
    const Token &c = peek();

    if (c.kind != Token::WORD)
    {
        error(c.line, "stage " + stageName.text +
              " wants a category::plugin");
        return false;
    }

    Token category = take();

    if (peek().kind != Token::MODSEP)
    {
        error(peek().line, "expected '::' after '" + category.text + "'");
        return false;
    }

    take();

    if (peek().kind != Token::WORD)
    {
        error(peek().line, "expected a plugin name after '" +
              category.text + "::'");
        return false;
    }

    Token plugName = take();

    std::map<std::string, thcPlugin *>::const_iterator found =
        plugins_.find(plugName.text);

    if (found == plugins_.end())
    {
        error(plugName.line, "no composer module called '" +
              plugName.text + "' is installed");
        return false;
    }

    thcPlugin *plugin = found->second;

    /* What the file asks of the plugin has to match what it exports --
       checked here, by name and line, because a generator that never
       ticks or a transformer that never receives would just be silence
       with no explanation. */
    if (category.text == "gen")
    {
        if (!plugin->hasTick())
        {
            error(plugName.line, "'" + plugName.text +
                  "' exports no tick; it cannot be a gen:: stage");
            return false;
        }
    }
    else if (category.text == "xform")
    {
        if (!plugin->hasReceive())
        {
            error(plugName.line, "'" + plugName.text +
                  "' exports no receive; it cannot be an xform:: stage");
            return false;
        }
    }
    else
    {
        error(category.line, "unknown stage category '" + category.text +
              "' (gen or xform)");
        return false;
    }

    if (!expectPunct('{'))
        return false;

    /* The category is the placement's role, not just a validation: a
       plugin exporting both entry points ticks as gen:: and only
       receives as xform::, and the scheduler has to be told which this
       is. */
    thcStage *stage = sched->addStage(chain, plugin,
                                      category.text == "gen");

    if (stage == NULL)
    {
        error(stageName.line, "'" + plugName.text +
              "' refused to create an instance");
        return false;
    }

    bool ok = true;

    while (true)
    {
        const Token &t = peek();

        if (t.kind == Token::PUNCT && t.text[0] == '}')
        {
            take();
            break;
        }

        if (t.kind == Token::END)
        {
            error(t.line, "stage " + stageName.text + ": unterminated body");
            return false;
        }

        if (!parseParam(sched, stage, stageName.text))
        {
            ok = false;

            /* Recover to the next `;' inside the block. */
            while (peek().kind != Token::END &&
                   !(peek().kind == Token::PUNCT &&
                     (peek().text[0] == ';' || peek().text[0] == '}')))
                take();

            if (peek().kind == Token::PUNCT && peek().text[0] == ';')
                take();
        }
    }

    if (!expectPunct(';'))
        ok = false;

    return ok;
}

bool
thcGenLoader::parseParam (thcScheduler *sched, thcStage *stage,
                          const std::string &stageName)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "stage " + stageName + ": expected a param name");
        return false;
    }

    Token pname = take();

    thcPlugin *plugin = stage->plugin;
    int idx = plugin->paramIndex(pname.text);

    if (idx < 0)
    {
        error(pname.line, "'" + plugin->name() +
              "' has no param called '" + pname.text + "'");
        return false;
    }

    const thcPlugin::ParamInfo *pi = plugin->paramInfo(idx);

    if (!expectPunct('='))
        return false;

    const Token &v = peek();

    if (v.kind == Token::NUMBER)
    {
        Token num = take();

        /* A unit is a WORD sitting right after the number. */
        std::string unit;

        if (peek().kind == Token::WORD &&
            (peek().text == "s" || peek().text == "ms" ||
             peek().text == "beats" || peek().text == "b"))
            unit = take().text;

        if (pi->isDuration())
        {
            /* The unit decides the clock, so its absence decides
               nothing -- which is exactly why it is an error. */
            if (unit.empty())
            {
                error(num.line, "'" + pname.text +
                      "' is a duration; write a unit (s, ms or beats)");
                return false;
            }

            /* The beats flag is stated on every write, not only when
               it is true: a param set to "4 beats" and later to "2 s"
               must stop tempo-scaling, and a flag only ever raised
               never comes down. */
            if (unit == "s")
                stage->params.set(idx, num.num);
            else if (unit == "ms")
                stage->params.set(idx, num.num / 1000.0);
            else                                 /* beats / b */
                stage->params.set(idx, num.num);

            stage->params.setBeats(idx, unit == "beats" || unit == "b");
        }
        else
        {
            if (!unit.empty())
            {
                error(num.line, "'" + pname.text +
                      "' is not a duration; a unit means nothing here");
                return false;
            }

            if (pi->type == THC_PARAM_NOTESET)
            {
                error(num.line, "'" + pname.text +
                      "' wants notes, not a bare number");
                return false;
            }

            stage->params.set(idx, num.num);
        }

        return expectPunct(';');
    }

    if (v.kind == Token::KNOB)
    {
        Token knobTok = take();
        thArg *knob = sched->knob(knobTok.text);

        if (knob == NULL)
        {
            error(knobTok.line, "'@" + knobTok.text +
                  "' is not a declared knob");
            return false;
        }

        if (pi->type == THC_PARAM_NOTESET || pi->type == THC_PARAM_STRING)
        {
            error(knobTok.line, "'" + pname.text +
                  "' is not numeric; a knob cannot drive it");
            return false;
        }

        sched->bindKnob(stage, idx, knob);

        return expectPunct(';');
    }

    if (v.kind == Token::STRING)
    {
        Token str = take();

        if (pi->type == THC_PARAM_NOTESET)
        {
            std::vector<int> resolved;
            std::string bad;

            if (!parseNoteList(str.text, resolved, bad))
            {
                error(str.line, "'" + pname.text + "': bad note name '" +
                      bad + "'");
                return false;
            }

            stage->params.setString(idx, noteListToString(resolved));
        }
        else if (pi->type == THC_PARAM_NOTE)
        {
            std::vector<int> resolved;
            std::string bad;

            if (!parseNoteList(str.text, resolved, bad) ||
                resolved.size() != 1)
            {
                error(str.line, "'" + pname.text + "' wants one note name");
                return false;
            }

            stage->params.set(idx, resolved[0]);
        }
        else if (pi->type == THC_PARAM_STRING)
            stage->params.setString(idx, str.text);
        else
        {
            error(str.line, "'" + pname.text +
                  "' is numeric; a string means nothing here");
            return false;
        }

        return expectPunct(';');
    }

    if (v.kind == Token::WORD)
    {
        /* A bare word as a value is a scale reference. */
        Token ref = take();

        if (pi->type != THC_PARAM_NOTESET)
        {
            error(ref.line, "'" + pname.text +
                  "' cannot take a scale; it is not a note set");
            return false;
        }

        std::map<std::string, std::vector<int> >::const_iterator s =
            scales_.find(ref.text);

        if (s == scales_.end())
        {
            error(ref.line, "no scale called '" + ref.text +
                  "' has been declared");
            return false;
        }

        stage->params.setString(idx, noteListToString(s->second));

        return expectPunct(';');
    }

    error(v.line, "stage " + stageName + ": '" + pname.text +
          "' has no value");
    return false;
}

bool
thcGenLoader::parseSinkBlock (thcScheduler *sched, size_t chain)
{
    if (!expectPunct('{'))
        return false;

    int channel = -1;
    std::string chanarg;

    while (true)
    {
        const Token &t = peek();

        if (t.kind == Token::PUNCT && t.text[0] == '}')
        {
            take();
            break;
        }

        if (t.kind == Token::END)
        {
            error(t.line, "unterminated sink");
            return false;
        }

        if (t.kind != Token::WORD ||
            (t.text != "channel" && t.text != "chanarg"))
        {
            error(t.line, "a sink says 'channel = N' and optionally "
                  "'chanarg = \"name\"'");
            return false;
        }

        Token key = take();

        if (!expectPunct('='))
            return false;

        if (key.text == "channel")
        {
            const Token &v = peek();

            if (v.kind != Token::NUMBER)
            {
                error(v.line, "channel wants a number");
                return false;
            }

            Token num = take();

            if (num.num < 0 || num.num > 15 ||
                num.num != (double)(int)num.num)
            {
                error(num.line, "channel is a whole number, 0-15");
                return false;
            }

            channel = (int)num.num;
        }
        else
        {
            const Token &v = peek();

            if (v.kind != Token::STRING)
            {
                error(v.line, "chanarg wants a quoted name");
                return false;
            }

            chanarg = take().text;
        }

        if (!expectPunct(';'))
            return false;
    }

    if (channel < 0)
    {
        error(peek().line, "sink has no channel");
        return false;
    }

    sched->addSink(chain, channel, chanarg);

    return expectPunct(';');
}
