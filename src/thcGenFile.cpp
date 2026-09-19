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
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "think.h"
#include "thLexer.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "thcNodeHost.h"

thcGenLoader::thcGenLoader (const std::map<std::string, thcPlugin *> &plugins)
    : plugins_(plugins), pos_(0), exprDepth_(0), meter_(4),
      sawSection_(false), sawSectionEnd_(false), hasSeed_(false), seed_(0)
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
 * that turns pitch text into numbers; plugins receive only the numbers.
 *
 * A `.' is a rest and resolves as -1: an entry that takes its turn and
 * sounds nothing. Every ladder already filters what it reads to 0..127,
 * so on one a rest is simply not a degree; a pool that is *cycled* --
 * gen::euclid's -- reads it as an onset with no note, which is what lets
 * a pool carry a rhythm as well as a progression. */
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

        if (text[i] == '.')
        {
            out.push_back(-1);
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

/* A resolved preset, in the form a plugin reads: "res=0.8,fmin=0.06".
 *
 * %.9g rather than a fixed precision: a float round-trips exactly at nine
 * significant digits, and a preset that came back as 0.059999999 after a
 * morph had interpolated between two of them would be a bug nobody would
 * think to look for here. */
static std::string
presetToString (const std::vector<std::pair<std::string, double> > &vec)
{
    std::ostringstream s;

    for (size_t i = 0; i < vec.size(); i++)
    {
        char num[32];

        std::snprintf(num, sizeof(num), "%.9g", vec[i].second);

        if (i)
            s << ",";

        s << vec[i].first << "=" << num;
    }

    return s.str();
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

/* The .dsp lexical layer, shared rather than reproduced.
 *
 * This used to be a hand-written scanner that copied thinklex.ll's rules
 * -- `#' comments, whitespace, numbers, words, quoted strings, `::', the
 * punctuation -- because linking the flex lexer would have meant
 * inheriting a grammar and a drawerful of globals along with it. The
 * globals are gone (thinklang is pure), and the lexer is now a
 * standalone thing in libthink that answers to neither language
 * (thLexer.h). So the copy is retired and this is an adapter over the
 * real one: two languages, one lexical layer, and nothing left for them
 * to drift apart along.
 *
 * The adapter does the three things .gen's shape asks for and .dsp's
 * does not:
 *
 *  - A leading `-' folds into the number after it. .gen has no
 *    arithmetic and so no grammar to hang a SUB token on; .dsp has
 *    both, which is why the shared lexer keeps them apart and this puts
 *    them back together. Adjacency is checked by span, which is what
 *    still makes `-5' a literal and `- 5' the error it always was.
 *  - `@name' folds into one KNOB token, spelled without the `@' and
 *    spanned with it. Same reason: `@' is an operator in a .dsp
 *    expression, and in .gen it is only ever the front of a knob's name.
 *  - Punctuation .gen has no use for is refused here rather than carried
 *    into the parser to be rejected further from the cause, and with the
 *    wording the hand-written scanner used: a `+' in a .gen file is a
 *    stray character, not a missing rule. `%' is on the useful side of
 *    that line, and only just: it is not arithmetic here, it is the
 *    unit suffix an instrument value needs when the chanarg it lands on
 *    was declared as a percentage. The parser refuses it everywhere
 *    else. `->' crosses the same way and for a better reason: it is the
 *    .dsp spelling for reading a node's output, and phase 3 gives a
 *    .gen nodes to read. One arrow, one meaning, both languages -- the
 *    shared lexer has always handed it back whole (longest match, so it
 *    was never `-' then `>'), and .gen simply stopped refusing it.
 *
 * Every token still carries its byte span, and a STRING's span still
 * includes its quotes: thcGenEdit replaces spans, and what sits between
 * them -- comments, indentation, the author's blank lines -- is never
 * touched. */
/* True if the last token emitted could be the end of a value, which is what
   decides whether a `-' after it is a sign or an operator. */
static bool
endsAValue (const std::vector<thcGenToken> &out)
{
    if (out.empty())
        return false;

    const thcGenToken &t = out[out.size() - 1];

    return t.kind == thcGenToken::NUMBER || t.kind == thcGenToken::KNOB ||
           t.kind == thcGenToken::WORD ||
           (t.kind == thcGenToken::PUNCT && t.text == ")");
}

bool
thcGenLoader::tokenize (const std::string &text, std::vector<thcGenToken> &out,
                        std::string &err, int &errLine)
{
    /* A public tokenizer owns its output: a caller reusing one vector
       across files must not get the previous file's tail. */
    out.clear();
    err.clear();
    errLine = 0;

    std::vector<thLexToken> raw;

    thLexString(text, raw);

    bool ok = true;

    /* raw always ends with an END token, so raw[i + 1] below is in range
       for every i this loop looks at a real token at. */
    for (size_t i = 0; i < raw.size(); i++)
    {
        const thLexToken &t = raw[i];

        if (t.kind == thLexToken::END)
            break;

        if (t.kind == thLexToken::ERROR)
        {
            err = t.text;
            errLine = t.line;
            ok = false;
            break;
        }

        Token g;

        g.line = t.line;
        g.num  = 0;
        g.off  = t.off;
        g.end  = t.end;
        g.text = t.text;

        if (t.kind == thLexToken::WORD)
            g.kind = Token::WORD;
        else if (t.kind == thLexToken::STRING)
            g.kind = Token::STRING;
        else if (t.kind == thLexToken::NUMBER)
        {
            g.kind = Token::NUMBER;
            g.num  = t.num;
        }
        else if (t.text == "::")
            g.kind = Token::MODSEP;
        else if (t.text == ";" || t.text == "=" || t.text == "{" ||
                 t.text == "}" || t.text == "." || t.text == "%" ||
                 t.text == "->" || t.text == "+" || t.text == "*" ||
                 t.text == "/" || t.text == "(" || t.text == ")" ||
                 t.text == ",")
            g.kind = Token::PUNCT;
        /* `-' fuses onto the number after it -- `= -5' is one token -- but
           only where nothing before it could have ended a value. `a - 5' and
           `a -5' are both a subtraction; without the second half of this test
           the latter lexed as two values in a row, which was an error message
           about a missing semicolon on a line whose semicolon is right
           there. */
        else if (t.text == "-" && raw[i + 1].kind == thLexToken::NUMBER &&
                 raw[i + 1].off == t.end && !endsAValue(out))
        {
            g.kind = Token::NUMBER;
            g.text = "-" + raw[i + 1].text;
            g.num  = -raw[i + 1].num;
            g.end  = raw[i + 1].end;
            i++;
        }
        else if (t.text == "-")
            g.kind = Token::PUNCT;
        else if (t.text == "@" && raw[i + 1].kind == thLexToken::WORD &&
                 raw[i + 1].off == t.end)
        {
            g.kind = Token::KNOB;
            g.text = raw[i + 1].text;
            g.end  = raw[i + 1].end;
            i++;
        }
        else if (t.text == "@")
        {
            err = "'@' with no knob name after it";
            errLine = t.line;
            ok = false;
            break;
        }
        else
        {
            err = std::string("stray character '") + t.text[0] + "'";
            errLine = t.line;
            ok = false;
            break;
        }

        out.push_back(g);
    }

    Token end;

    end.kind = Token::END;
    end.line = ok ? raw[raw.size() - 1].line : errLine;
    end.num  = 0;
    end.off  = end.end = text.size();
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

/* The same idea one level in: recover to just past the next `;', or stop
 * at the `}' that ends the block. */
void
thcGenLoader::skipToNextInBlock (void)
{
    while (peek().kind != Token::END &&
           !(peek().kind == Token::PUNCT &&
             (peek().text[0] == ';' || peek().text[0] == '}')))
        take();

    if (peek().kind == Token::PUNCT && peek().text[0] == ';')
        take();
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
    presets_.clear();
    instruments_.clear();
    instrumentLines_.clear();
    claimedChannels_.clear();
    pendingSinks_.clear();
    pendingNodeBinds_.clear();
    sectionLines_.clear();
    meter_ = 4;
    sawSection_ = false;
    sawSectionEnd_ = false;
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

    /* Channels, then the instruments themselves -- in that order and
       both after the parse, because an instrument cannot know its
       channel until every `channel = N' in the file has been read, and
       cannot be loaded onto one until it has one. Neither runs if the
       parse already failed: there is nothing to allocate for a file
       that is not going to load, and loading a graph on the strength of
       a piece with an error in it would put sound on a channel nobody
       asked for. */
    size_t applied = 0;

    if (errors_.empty() && allocateChannels(sched))
        while (applied < sched->instruments().size())
        {
            std::string why;

            if (!sched->applyInstrument(applied, why))
            {
                error(applied < instrumentLines_.size()
                      ? instrumentLines_[applied] : 0,
                      "instrument '" + sched->instruments()[applied].name +
                      "': " + why);

                /* Stop at the first one. The file is not going to load
                   now, and every instrument after this would be another
                   graph put on another channel for a piece nobody is
                   going to hear -- one wrong answer is easier to read
                   than five, and cheaper to take back.
                 *
                   Nothing to undo for this one: applyInstrument is all
                   or nothing, and a value refused after its graph was
                   loaded takes that graph back itself. Which is why
                   `applied' is an exact count of what is still up, and
                   the rollback below can be the obvious loop rather than
                   a loop plus a flag about the index it stopped on. */
                break;
            }

            applied++;
        }

    /* And the graph on the mix, after the instruments for the reason they
       come after the channels: it is the last thing the file says about
       where the sound goes. Applied even when the file declares none,
       because that is what takes the last piece's off. */
    bool master = false;

    if (errors_.empty())
    {
        std::string why;

        if (sched->applyMasterEffect(why))
            master = !sched->masterEffect().effect.empty();
        else
            error(0, "the master effect: " + why);
    }

    if (errors_.empty())
        checkSinkArgs(sched);

    if (errors_.empty())
        checkSections(sched);

    if (errors_.empty())
        bindNodes(sched);

    if (!errors_.empty())
    {
        /* A file with any error loads nothing.
         *
         * That was true of the chains from the first version of this
         * loader and briefly false of the channels: an instrument that
         * came up before a later one failed stayed up, silent, on a tab,
         * for a piece nobody was going to hear. So the ones that made it
         * are taken back before the chains are -- in reverse, which
         * costs nothing and is the order anyone reading this expects.
         *
         * One of them can refuse to go: unapplyInstrument returns false
         * when the audio thread could not be told, and then the graph is
         * still on that channel and still sounding. Said rather than
         * swallowed -- "the file loaded nothing" would be a lie, and the
         * one place a person would look for the truth is the same list
         * of errors that explains why the load failed at all. The
         * scheduler keeps the instrument and retries on its own clock;
         * this is only the telling. */
        if (master && !sched->unapplyMasterEffect())
            error(0, "the master effect is still on the mix; the audio "
                  "thread could not be told to drop it");

        while (applied > 0)
            if (!sched->unapplyInstrument(--applied))
                error(applied < instrumentLines_.size()
                      ? instrumentLines_[applied] : 0,
                      "instrument '" + sched->instruments()[applied].name +
                      "' is still on channel " +
                      std::to_string(sched->instruments()[applied].channel + 1)
                      + "; the audio thread could not be told to drop it, "
                      "and it will be retried");

        sched->clearChains();
        return false;
    }

    return true;
}

/* Every chain a section names, against the chains the file declares.
 *
 * A section names chains by name and is written above them, so the name
 * cannot be checked as it is read. Left unchecked it would be the
 * quietest mistake in the language: `section break 4 bars { kik = 0; }'
 * loads, plays, and does nothing at all -- the breakdown is four more
 * bars of everything, and nothing anywhere says why.
 */
void
thcGenLoader::checkSections (thcScheduler *sched)
{
    const std::vector<thcSection> &secs = sched->sections();

    for (size_t i = 0; i < secs.size(); i++)
    {
        const int line = i < sectionLines_.size() ? sectionLines_[i] : 0;

        for (size_t k = 0; k < secs[i].levels.size(); k++)
        {
            const std::string &want = secs[i].levels[k].first;
            bool found = false;

            for (size_t ci = 0; ci < sched->chainCount() && !found; ci++)
                found = sched->chain(ci)->name == want;

            if (!found)
                error(line, "section '" + secs[i].name + "' names '" +
                      want + "', which is not a chain in this piece");
        }
    }
}

/* A sink bound to an instrument names a knob on a graph this piece just
 * loaded -- so unlike a `channel = N' sink, whose patch is somebody
 * else's business and may not even be loaded yet, this one can be
 * checked. And should be: the name is overwritten onto every event
 * passing through, and a knob the graph does not declare means
 * getChanArg returns NULL at delivery and the sink silently does
 * nothing, forever, a long way from the typo. The same argument the sink
 * parser already makes about `cut off' with a space in it, now that
 * there is something to check the name against.
 *
 * `*' is exempt: it means the events name their own targets, which are
 * a composer's business and not knowable here.
 */
void
thcGenLoader::checkSinkArgs (thcScheduler *sched)
{
    for (size_t i = 0; i < pendingSinks_.size(); i++)
    {
        const PendingSink &p = pendingSinks_[i];
        const thcInstrument *inst = sched->instrument(p.instrument);
        thcChain *c = sched->chain(p.chain);

        if (inst == NULL || c == NULL || p.sink >= c->sinks.size())
            continue;

        const thcSink &s = c->sinks[p.sink];

        if (!s.isChanarg() || s.namesItsOwn())
            continue;

        if (!sched->chanArgExists(inst->channel, s.chanarg))
        {
            /* Which of the two graphs the name was aimed at. `fx.mix' is
               the effect's, and naming the instrument's .dsp here would
               send the author to a file that was never going to declare
               it -- the same split, for the same reason, that
               thcScheduler::writeValues makes over an `effect' block's
               own values. The prefix comes off the quoted name too: what
               the author has to go and read is `mix' in the effect. */
            const size_t plen = strlen(TH_EFFECT_PREFIX);
            const bool prefixed =
                s.chanarg.compare(0, plen, TH_EFFECT_PREFIX) == 0;

            error(p.line, prefixed
                  ? "instrument '" + p.instrument + "' has effect '" +
                    inst->effect + "', which declares no chanarg called '" +
                    s.chanarg.substr(plen) + "'"
                  : "instrument '" + p.instrument + "' is '" +
                    inst->dsp + "', which declares no chanarg called '" +
                    s.chanarg + "'");
            continue;
        }

        /* Two things driving one knob, and one of them is a human hand.
         *
         * A knob binding is a push and so is a chanarg sink, so both
         * writing the same arg is last-writer-wins -- which in practice
         * means the walk wins, every time it fires, and the slider
         * appears to do nothing a second after you let go of it. There
         * is no reading of the file where that was the intention, and it
         * is invisible from either end. If what was wanted is a starting
         * point the walk moves away from, that is a plain number. */
        for (size_t k = 0; k < inst->args.size(); k++)
            if (inst->args[k].name == s.chanarg &&
                !inst->args[k].knob.empty())
                error(p.line, "'" + s.chanarg + "' on instrument '" +
                      p.instrument + "' is already driven by '@" +
                      inst->args[k].knob + "'; a sink and a knob would "
                      "fight over it (write a plain number for a "
                      "starting point)");
    }
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

    if (t.text == "preset")
    {
        take();
        return parsePreset();
    }

    if (t.text == "meter")
    {
        take();
        return parseMeter();
    }

    if (t.text == "section")
    {
        take();
        return parseSection(sched);
    }

    if (t.text == "instrument")
    {
        take();
        return parseInstrument(sched);
    }

    if (t.text == "effect")
    {
        Token key = take();

        return parseMasterEffect(sched, key);
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

/* `preset warm { res = 0.8; fmin = 0.06; };'
 *
 * A named vector of chanarg values -- the noun COMPOSITION_HANDOFF.md §9
 * says tier 2 was missing. A patch's declared chanargs are a vector of
 * floats, and a vector of floats is something a morph can interpolate, a
 * GA can breed and a file can save; without a name for one, every
 * composer that wants to move a timbre has to carry the whole vector in
 * its own params.
 *
 * Deliberately plain numbers. A knob inside a preset would make it not a
 * vector but an expression that happens to have a value right now, and
 * the two things a preset is for -- interpolating between them, and
 * saving them -- both want the fixed reading. The knob belongs on the
 * stage that *uses* the preset, where it already works.
 *
 * Declaration order is kept (see Preset in the header): the vector is the
 * point, and the panel that eventually draws one should show it the way
 * its author grouped it.
 */
bool
thcGenLoader::parsePreset (void)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "preset wants a name");
        return false;
    }

    Token nameTok = take();

    if (presets_.find(nameTok.text) != presets_.end())
    {
        error(nameTok.line, "preset '" + nameTok.text +
              "' is already declared");
        return false;
    }

    if (!expectPunct('{'))
        return false;

    Preset vec;

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
            error(t.line, "unterminated preset '" + nameTok.text + "'");
            return false;
        }

        if (t.kind != Token::WORD)
        {
            error(t.line, "preset " + nameTok.text +
                  ": expected a chanarg name");
            return false;
        }

        Token arg = take();

        for (size_t i = 0; i < vec.size(); i++)
            if (vec[i].first == arg.text)
            {
                error(arg.line, "preset " + nameTok.text + " sets '" +
                      arg.text + "' twice");
                return false;
            }

        if (!expectPunct('='))
            return false;

        const Token &v = peek();

        if (v.kind == Token::KNOB)
        {
            /* Named so the message says what to do instead. A preset
               bound to a knob is not a vector, and the two things a
               preset exists for both need it to be one. */
            error(v.line, "preset " + nameTok.text + ": '" + arg.text +
                  "' cannot be a knob; a preset is a fixed vector");
            return false;
        }

        if (v.kind != Token::NUMBER)
        {
            error(v.line, "preset " + nameTok.text + ": '" + arg.text +
                  "' wants a number");
            return false;
        }

        vec.push_back(std::make_pair(arg.text, take().num));

        if (!expectPunct(';'))
            return false;
    }

    if (vec.empty())
    {
        /* An empty preset is almost certainly half an edit, and a morph
           between two of them would sweep nothing at all -- silently,
           which is the failure worth refusing. */
        error(nameTok.line, "preset '" + nameTok.text + "' sets nothing");
        return false;
    }

    presets_[nameTok.text] = vec;

    return expectPunct(';');
}

/* `meter 4;' -- beats to a bar, and nothing else.
 *
 * It exists so a section's length can be written in bars, which is how
 * an arrangement is thought about and counted. Nothing else in the
 * language reads it: a stage's `period = 1 beats' means a beat here as
 * it does everywhere.
 *
 * Before the first section, for the same reason a seed comes before the
 * first chain: bars are folded to beats as each section is read, so a
 * meter below one could not mean what it says.
 */
bool
thcGenLoader::parseMeter (void)
{
    const Token &v = peek();

    if (v.kind != Token::NUMBER)
    {
        error(v.line, "meter wants a number of beats to a bar");
        return false;
    }

    Token n = take();

    if (sawSection_)
    {
        error(n.line, "meter must come before the first section");
        return false;
    }

    if (n.num <= 0 || n.num != std::floor(n.num) || n.num > 64)
    {
        error(n.line, "meter is a whole number of beats, 1 to 64");
        return false;
    }

    meter_ = n.num;

    return expectPunct(';');
}

/* `section drop 16 bars { kick = 1; lead = 1.2; };' and `section end;'
 *
 * The arrangement (GEN_FORMAT.md §5c): where the piece goes, written
 * once and in order, instead of eight xform::form patterns under eight
 * chains that somebody has to keep in step by hand.
 *
 * A length in bars is folded to beats here, through `meter', so the
 * scheduler has one unit to convert and the tempo means the same thing
 * to a section as it does to a `period'. `s' is kept for a piece with no
 * pulse, where a bar is not a thing.
 *
 * The chain names inside are not resolved here. An arrangement belongs
 * at the top of a file, above the chains it arranges, so the names it
 * gives are almost always forward references; checkSections looks them
 * up once the file has been read, which is when the answer exists.
 */
bool
thcGenLoader::parseSection (thcScheduler *sched)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "section wants a name");
        return false;
    }

    Token nameTok = take();

    if (sawSectionEnd_)
    {
        error(nameTok.line,
              "nothing comes after 'section end' -- the piece stops there");
        return false;
    }

    /* `section end;' -- the list is closed. A section actually called
       `end' would read as this one to every person who opened the file,
       so the name is spent. */
    if (nameTok.text == "end")
    {
        if (!(peek().kind == Token::PUNCT && peek().text[0] == ';'))
        {
            error(nameTok.line, "'end' closes the section list and cannot "
                  "be the name of a section");
            return false;
        }

        if (!sawSection_)
        {
            error(nameTok.line,
                  "'section end' with no sections before it");
            return false;
        }

        sawSectionEnd_ = true;
        sched->endAfterSections(true);

        return expectPunct(';');
    }

    for (size_t i = 0; i < sched->sections().size(); i++)
        if (sched->sections()[i].name == nameTok.text)
        {
            error(nameTok.line, "section '" + nameTok.text +
                  "' is already declared");
            return false;
        }

    const Token &lenTok = peek();

    if (lenTok.kind != Token::NUMBER)
    {
        error(lenTok.line, "section '" + nameTok.text +
              "' wants a length");
        return false;
    }

    Token len = take();

    if (peek().kind != Token::WORD ||
        (peek().text != "bars" && peek().text != "beats" &&
         peek().text != "b" && peek().text != "s"))
    {
        error(len.line, "section '" + nameTok.text + "' is a length; "
              "write a unit (bars, beats or s)");
        return false;
    }

    Token unit = take();

    if (len.num <= 0)
    {
        error(len.line, "section '" + nameTok.text +
              "' lasts no time at all");
        return false;
    }

    thcSection sec;

    sec.name = nameTok.text;
    sec.beats = unit.text != "s";
    sec.length = unit.text == "bars" ? len.num * meter_ : len.num;

    if (!expectPunct('{'))
        return false;

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
            error(t.line, "unterminated section '" + sec.name + "'");
            return false;
        }

        if (t.kind != Token::WORD)
        {
            error(t.line, "section " + sec.name +
                  ": expected a chain name");
            return false;
        }

        Token chain = take();

        for (size_t i = 0; i < sec.levels.size(); i++)
            if (sec.levels[i].first == chain.text)
            {
                error(chain.line, "section " + sec.name + " sets '" +
                      chain.text + "' twice");
                return false;
            }

        if (!expectPunct('='))
            return false;

        const Token &v = peek();

        if (v.kind != Token::NUMBER)
        {
            /* A knob here would make the arrangement something that
               changes while the piece plays, which is not what a
               section is: it is the shape the piece has. */
            error(v.line, "section " + sec.name + ": '" + chain.text +
                  "' wants a number -- 0 to mute it, 1 as written");
            return false;
        }

        Token level = take();

        if (level.num < 0)
        {
            error(level.line, "section " + sec.name + ": '" + chain.text +
                  "' cannot be negative");
            return false;
        }

        sec.levels.push_back(std::make_pair(chain.text, level.num));

        if (!expectPunct(';'))
            return false;
    }

    sawSection_ = true;
    sectionLines_.push_back(nameTok.line);
    sched->addSection(sec);

    return expectPunct(';');
}

/* `instrument pad { dsp "amb01.dsp"; a = 900 ms; fmin = 0.06; };'
 *
 * The block that makes a piece self-contained. Until this existed, a
 * .gen named MIDI channels and left what was on them to whoever opened
 * the file -- which is why every shipped piece carries a paragraph at
 * the top saying what to go and load first, and why "open it and press
 * play" was never true of any of them.
 *
 * The shape is a .patch said out loud: a graph, then the values that
 * make it this instrument rather than that graph's defaults. Two things
 * a .patch cannot do, and this can:
 *
 *  - The values carry units. A .patch stores every chanarg already
 *    folded, which is how you end up with `a 39690' in a file people
 *    are supposed to read. Here it is `a = 900 ms', and the fold
 *    happens against the arg's own declared unit at the rate the synth
 *    is running -- see thcScheduler::applyInstrument, which is also
 *    where a unit that does not match the arg gets refused.
 *  - It has a name, and a sink can bind to the name. That is the whole
 *    point: routing stops being a number the author and the listener
 *    have to agree about out of band.
 *
 *  - A value may be a knob. `fmin = @warmth;' is the same @warmth a
 *    stage param binds to, reaching a composer and an instrument from
 *    one slider -- UNIFICATION.md phase 2, and the reason this block
 *    and the chains below it belong in one file at all. The binding
 *    carries a unit exactly as a literal does, because the number in a
 *    knob is exactly as unitless as the number in a file; what a unit
 *    means is checked where the value lands, in
 *    thcScheduler::applyInstrument.
 *
 * Not here: the graph written out inline instead of named. By reference
 * alone delivers the self-contained file, which is what this block was
 * for; inlining is the half that wants the grammar merge.
 */
/* `a = 900 ms;' or `delay = @throw ms;', inside an instrument block or
 * inside the effect block within it.
 *
 * `prefix' is empty for the instrument's own values and TH_EFFECT_PREFIX for
 * the effect's, and it goes on the *name*, so that one list of values reaches
 * two chanarg maps and thcScheduler writes both through the one call it
 * already makes. `fx.delay' is the string the engine takes.
 */
bool
thcGenLoader::parseInstrumentValue (thcScheduler *sched, thcInstrument &inst,
                                    const std::string &where,
                                    const Token &key,
                                    const std::string &prefix)
{
    const std::string name = prefix + key.text;

    for (size_t i = 0; i < inst.args.size(); i++)
        if (inst.args[i].name == name)
        {
            error(key.line, where + " sets '" + name + "' twice");
            return false;
        }

    if (!expectPunct('='))
        return false;

    const Token &v = peek();

    if (v.kind != Token::NUMBER && v.kind != Token::KNOB)
    {
        error(v.line, where + ": '" + name + "' wants a number or a "
              "knob");
        return false;
    }

    thcInstrumentArg a;
    Token val = take();

    a.name = name;

    if (val.kind == Token::KNOB)
    {
        /* Declared first, like everywhere else a knob is named. */
        if (sched->knob(val.text) == NULL)
        {
            error(val.line, "'@" + val.text + "' is not a declared knob");
            return false;
        }

        a.knob  = val.text;
        a.value = 0;
    }
    else
        a.value = val.num;

    /* The two units the language folds. `s' and `beats' are the
       composer's units and mean nothing on this side of the
       boundary: a chanarg is a number the audio thread reads, not a
       duration the transport schedules. Which unit an arg wants is
       the arg's own business and is checked when the value lands --
       here we only record what was written.
     *
       A knob binding carries one for exactly the same reason a
       literal does. The number a knob holds is as unitless as the
       number in the file, so `a = @attack' with nothing after it
       would be a slider quietly running in samples; the unit says
       what the knob's numbers mean, and it is applied on every move
       rather than once. */
    if (peek().kind == Token::WORD && peek().text == "ms")
        a.units = take().text;
    else if (peek().kind == Token::PUNCT && peek().text[0] == '%')
    {
        take();
        a.units = "%";
    }

    inst.args.push_back(a);

    return expectPunct(';');
}

/* `side = carrier;' inside an effect block.
 *
 * The second thing an effect can hear. An effect is handed the sum of its own
 * channel's voices and nothing else, which is enough for a delay and not
 * enough for anything that compares two signals: a vocoder wants a carrier
 * and a modulator, a compressor keyed off the kick wants the kick. This names
 * the other one, by instrument, and the engine writes that channel's output
 * into the effect graph's side0..side<N-1>.
 *
 * An instrument, declared before it is named, like a scale or a preset --
 * which is also what makes a ring impossible to write: an instrument cannot
 * name itself, because it is not declared until its own block is closed, and
 * it cannot name a later one at all. The engine refuses a cycle again at
 * load, because a host may put an effect anywhere.
 *
 * The number behind the name is not known yet. Channels are allocated after
 * the whole file has been read -- see allocateChannels -- so what is recorded
 * here is the name, and the channel is filled in there with the sinks'.
 */
bool
thcGenLoader::parseEffectSide (thcInstrument &inst, const std::string &where,
                               const Token &key, bool sideOK)
{
    if (!sideOK)
    {
        error(key.line, where + " cannot name a side: it is on the mix, "
              "which is every channel already");
        return false;
    }

    if (!inst.side.empty())
    {
        error(key.line, where + "'s effect names two sides");
        return false;
    }

    if (!expectPunct('='))
        return false;

    const Token &v = peek();

    if (v.kind != Token::WORD)
    {
        error(v.line, where + ": 'side' wants the name of an instrument");
        return false;
    }

    Token val = take();

    if (instruments_.find(val.text) == instruments_.end())
    {
        error(val.line, where + ": '" + val.text + "' is not a declared "
              "instrument");
        return false;
    }

    inst.side = val.text;

    return expectPunct(';');
}

/* `effect "echo.dsp" { delay = 375 ms; feedback = 0.45; };'
 *
 * The second graph an instrument can name: not the one that makes its notes
 * but the one that runs on the sum of them, once per window, whether or not
 * a note is sounding. A delay throw that outlives the note is the case --
 * DSP_FORMAT.md's "An effect graph" says what one is.
 *
 * Inside the instrument block rather than beside it, because an effect
 * belongs to a channel and it is the instrument that has one. The values are
 * the effect's chanargs and are recorded under `fx.' so that they cannot
 * collide with the instrument's; everything else about them -- units, knob
 * bindings, when they are checked -- is what a value in this block already
 * is.
 *
 * The braces are optional: an effect with nothing to say is
 * `effect "echo.dsp";'.
 */
bool
thcGenLoader::parseInstrumentEffect (thcScheduler *sched, thcInstrument &inst,
                                     const std::string &where,
                                     const Token &key,
                                     const std::string &prefix,
                                     bool sideOK)
{
    if (!inst.effect.empty())
    {
        error(key.line, where + " names two effects");
        return false;
    }

    const Token &v = peek();

    if (v.kind != Token::STRING)
    {
        error(v.line, where + ": effect wants a quoted filename");
        return false;
    }

    Token file = take();

    if (file.text.empty())
    {
        error(file.line, where + ": effect wants a filename");
        return false;
    }

    inst.effect = file.text;

    if (peek().kind == Token::PUNCT && peek().text[0] == '{')
    {
        take();

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
                error(t.line, "unterminated effect in " + where);
                return false;
            }

            if (t.kind != Token::WORD)
            {
                error(t.line, where + ": expected a chanarg name inside "
                      "effect");
                return false;
            }

            Token inner = take();

            /* A keyword inside the block, for the reason `dsp' and `effect'
               are keywords outside it: what it names is another instrument
               rather than a number, and an effect that declared a chanarg
               called @side would otherwise shadow it. */
            if (inner.text == "side")
            {
                if (!parseEffectSide(inst, where, inner, sideOK))
                    return false;

                continue;
            }

            if (!parseInstrumentValue(sched, inst, where, inner, prefix))
                return false;
        }
    }

    return expectPunct(';');
}

bool
thcGenLoader::parseInstrument (thcScheduler *sched)
{
    const Token &n = peek();

    if (n.kind != Token::WORD)
    {
        error(n.line, "instrument wants a name");
        return false;
    }

    Token nameTok = take();

    if (instruments_.find(nameTok.text) != instruments_.end())
    {
        error(nameTok.line, "instrument '" + nameTok.text +
              "' is already declared");
        return false;
    }

    if (!expectPunct('{'))
        return false;

    thcInstrument inst;

    inst.name = nameTok.text;

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
            error(t.line, "unterminated instrument '" + nameTok.text + "'");
            return false;
        }

        if (t.kind != Token::WORD)
        {
            error(t.line, "instrument " + nameTok.text +
                  ": expected 'dsp' or a chanarg name");
            return false;
        }

        Token key = take();

        /* `dsp' is a keyword inside this block rather than a chanarg
           that happens to take a string. A graph called @dsp would be a
           strange thing to declare and this would shadow it; naming the
           graph is what an instrument is *for*, so it gets the word. */
        if (key.text == "dsp")
        {
            if (!inst.dsp.empty())
            {
                error(key.line, "instrument " + nameTok.text +
                      " names two dsp files");
                return false;
            }

            const Token &v = peek();

            if (v.kind != Token::STRING)
            {
                error(v.line, "instrument " + nameTok.text +
                      ": dsp wants a quoted filename");
                return false;
            }

            Token file = take();

            if (file.text.empty())
            {
                error(file.line, "instrument " + nameTok.text +
                      ": dsp wants a filename");
                return false;
            }

            inst.dsp = file.text;

            if (!expectPunct(';'))
                return false;

            continue;
        }

        /* A keyword for the same reason `dsp' is one: what it names is a
           file, and an instrument that declared a chanarg called @effect
           would otherwise shadow it. */
        if (key.text == "effect")
        {
            if (!parseInstrumentEffect(sched, inst,
                                       "instrument " + nameTok.text, key,
                                       TH_EFFECT_PREFIX, true))
                return false;

            continue;
        }

        if (!parseInstrumentValue(sched, inst, "instrument " + nameTok.text,
                                  key, ""))
            return false;
    }

    if (inst.dsp.empty())
    {
        /* An instrument with values and no graph is half an edit. It
           would allocate a channel, load nothing onto it, and then fail
           to find every arg it names -- five confusing errors instead of
           the one true one. */
        error(nameTok.line, "instrument '" + nameTok.text +
              "' names no dsp");
        return false;
    }

    instruments_[nameTok.text] = sched->addInstrument(inst);
    instrumentLines_.push_back(nameTok.line);

    return expectPunct(';');
}

/* `effect "fx/limiter.dsp" { ceiling = 0.9; };' at the top level.
 *
 * The same clause an instrument carries, aimed at the mix instead of at a
 * channel: after every voice on every channel has been summed and before the
 * master gain and the limiter. A reverb belongs here -- one room rather than
 * one per channel, each paying for its own -- and a limiter can be nowhere
 * else, since the thing it is limiting is the sum.
 *
 * Its values carry no `fx.' prefix. That prefix exists to keep an
 * instrument's chanargs and its effect's apart on one channel, and on the mix
 * there is no instrument to collide with.
 */
bool
thcGenLoader::parseMasterEffect (thcScheduler *sched, const Token &key)
{
    if (!sched->masterEffect().effect.empty())
    {
        error(key.line, "the piece names two master effects");
        return false;
    }

    thcInstrument master;

    master.name = "the mix";
    master.channel = -1;

    if (!parseInstrumentEffect(sched, master, "the master effect", key, "",
                               false))
        return false;

    sched->setMasterEffect(master.effect, master.args);

    return true;
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

    /* The chain's nodes, now that every name in it is known -- a wire
       may point forwards, exactly as one in a .dsp may, so nothing can
       be resolved until the body has been read to its end. */
    thcChain *c = sched->chain(chain);

    if (ok && c != NULL && c->nodes)
    {
        std::string why;

        if (!c->nodes->build(why))
        {
            error(nameTok.line, "chain " + nameTok.text + ": " + why);
            ok = false;
        }
    }

    return ok;
}

/* `stage lfo osc::simple { freq = 0.05; in0 = other->out; };'
 *
 * The body is a .dsp node's body, plus knobs: numbers, arrows to other
 * nodes, and `@name'. No units (a chanarg's `ms' is about a rate this
 * host is not running at, and `s' and `beats' are the transport's) and
 * no scales -- everything a node can be told is a number, and the three
 * spellings here are the three ways a piece has of naming one.
 *
 * The knob is the one thing a .dsp body cannot say, and phase 2 is why
 * it is here: a knob means the same thing on both sides of the
 * composer/instrument boundary, and leaving the nodes out of that would
 * have made an LFO's depth the one number in a piece that could not go
 * on a slider.
 *
 * The host does the loading and the refusing: whether a category means
 * anything one sample at a time is its judgement, argued where it is
 * made. This turns the file into calls and reports what comes back
 * against the line that caused it.
 */
/* ---- arithmetic over signals -------------------------------------------
 *
 * See the declarations in thcGenFile.h. The shape of these three functions
 * is thinklang.yy's, rule for rule, so that `a - b - c' and `a / b / c'
 * group the same way in both languages -- right-associative, which is what
 * .dsp has always done. Reproducing that is the point: one language should
 * not read two ways depending on which file it is in.
 *
 * It did once. `-60 + 100' was 40 here and -160 there, because .dsp put its
 * unary minus at the top of an expression where it scoped over everything to
 * the right, and parseExprFactor binds it to its operand. thinklang.yy has a
 * `factor: SUB factor' now and the two agree; exprcheck and gencheck fold the
 * same list of expressions so that they go on agreeing.
 */

/* True if the value about to be read has an operator in it.
 *
 * Looks ahead to the `;' at paren depth zero rather than trying the
 * expression grammar and backtracking: a `.gen' param may be a note list or
 * a preset name, and a parser that had to fail on those first would report
 * arithmetic errors about words that were never meant to be arithmetic.
 *
 * `->' cannot be mistaken for a minus -- the tokenizer emits it whole -- and
 * a `-' glued to a number is part of the number, so a bare `-' here is
 * always a subtraction. */
bool
thcGenLoader::aheadIsExpression (void) const
{
    /* A call and a parenthesised value carry no operator of their own --
       `clamp(abs(x), 0.2, 0.8)' is arithmetic with none in it. */
    if (pos_ < tokens_.size())
    {
        const Token &t = tokens_[pos_];

        if (t.kind == Token::PUNCT && t.text == "(")
            return true;

        if (t.kind == Token::WORD && pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].kind == Token::PUNCT &&
            tokens_[pos_ + 1].text == "(")
            return true;
    }

    int depth = 0;

    for (size_t i = pos_; i < tokens_.size(); i++)
    {
        const Token &t = tokens_[i];

        if (t.kind == Token::END)
            break;

        if (t.kind != Token::PUNCT)
            continue;

        if (t.text == "(")
        { depth++; continue; }

        if (t.text == ")")
        { depth--; continue; }

        if (depth == 0 && (t.text == ";" || t.text == "}"))
            break;

        if (t.text == "+" || t.text == "-" || t.text == "*" || t.text == "/")
            return true;
    }

    return false;
}

/* The expression parser's depth, put back on every path out of a frame.
   See exprDepth_ in the header. */
namespace {
    struct ExprDepth
    {
        int &n;

        ExprDepth (int &counter) : n(counter) { n++; }
        ~ExprDepth (void) { n--; }
    };
}

static const int TOO_DEEP = 200;

static bool
isOp (const thcGenToken &t, char c)
{
    return t.kind == thcGenToken::PUNCT && t.text.size() == 1 &&
           t.text[0] == c;
}

thExprNode *
thcGenLoader::parseExprFactor (thcScheduler *sched)
{
    ExprDepth depth(exprDepth_);

    if (exprDepth_ > TOO_DEEP)
    {
        error(peek().line, "this expression is nested too deeply");
        return NULL;
    }

    const Token &t = peek();

    if (isOp(t, '('))
    {
        take();

        thExprNode *inner = parseExpr(sched);

        if (inner == NULL)
            return NULL;

        if (!isOp(peek(), ')'))
        {
            error(peek().line, "expected ')'");
            thExprFree(inner);
            return NULL;
        }

        take();

        return inner;
    }

    if (isOp(t, '-'))
    {
        /* `- x' is `x * -1', the node that already exists. Reached only
           where the tokenizer left the `-' standing -- a `-' glued to a
           number is part of it. */
        take();

        thExprNode *inner = parseExprFactor(sched);

        if (inner == NULL)
            return NULL;

        return thExprOp('*', inner, thExprConst(-1));
    }

    if (t.kind == Token::NUMBER)
        return thExprConst((float)take().num);

    if (t.kind == Token::KNOB)
    {
        Token k = take();

        /* Checked here rather than at the desugar so the error names the
           line the knob is written on. */
        if (sched->knob(k.text) == NULL)
        {
            error(k.line, "'@" + k.text + "' is not a declared knob");
            return NULL;
        }

        return thExprChanRef(k.text);
    }

    if (t.kind == Token::WORD)
    {
        Token w = take();

        if (isOp(peek(), '('))
        {
            /* `exp2(@cents / 1200)'. The arity is the function's; a call
               written with the wrong number of arguments is reported by
               thExprCall against the name rather than as a stray comma. */
            take();

            std::vector<thExprNode *> kids;
            bool bad = false;

            if (!isOp(peek(), ')'))
                for (;;)
                {
                    thExprNode *arg = parseExpr(sched);

                    if (arg == NULL)
                    { bad = true; break; }

                    kids.push_back(arg);

                    if (!isOp(peek(), ','))
                        break;

                    take();
                }

            if (!bad && !isOp(peek(), ')'))
            {
                error(peek().line, "expected ')' after " + w.text + "(");
                bad = true;
            }

            if (bad)
            {
                for (size_t i = 0; i < kids.size(); i++)
                    thExprFree(kids[i]);

                return NULL;
            }

            take();

            std::string why;
            thExprNode *call = thExprCall(w.text, kids, why);

            if (call == NULL)
                error(w.line, why);

            return call;
        }

        if (!(peek().kind == Token::PUNCT && peek().text == "->"))
        {
            error(w.line, "'" + w.text + "' is not a value here; reading a "
                  "node is spelled '" + w.text + "->out'");
            return NULL;
        }

        take();

        if (peek().kind != Token::WORD)
        {
            error(peek().line, "expected an arg name after '" + w.text +
                  "->'");
            return NULL;
        }

        Token a = take();

        return thExprNodeRef(w.text, a.text);
    }

    error(t.line, "expected a number, a knob or a node's output");

    return NULL;
}

thExprNode *
thcGenLoader::parseExprTerm (thcScheduler *sched)
{
    ExprDepth depth(exprDepth_);

    if (exprDepth_ > TOO_DEEP)
    {
        error(peek().line, "this expression is nested too deeply");
        return NULL;
    }

    thExprNode *left = parseExprFactor(sched);

    if (left == NULL)
        return NULL;

    if (!isOp(peek(), '*') && !isOp(peek(), '/'))
        return left;

    const Token op = take();

    thExprNode *right = parseExprTerm(sched);

    if (right == NULL)
    {
        thExprFree(left);
        return NULL;
    }

    return thExprOp(op.text[0], left, right);
}

thExprNode *
thcGenLoader::parseExpr (thcScheduler *sched)
{
    ExprDepth depth(exprDepth_);

    if (exprDepth_ > TOO_DEEP)
    {
        error(peek().line, "this expression is nested too deeply");
        return NULL;
    }

    thExprNode *left = parseExprTerm(sched);

    if (left == NULL)
        return NULL;

    if (!isOp(peek(), '+') && !isOp(peek(), '-'))
        return left;

    const Token op = take();

    thExprNode *right = parseExpr(sched);

    if (right == NULL)
    {
        thExprFree(left);
        return NULL;
    }

    return thExprOp(op.text[0], left, right);
}

bool
thcGenLoader::emitExpr (thcScheduler *sched, thcNodeHost *host,
                        const thExprNode *e, const std::string &base,
                        int &serial, ExprRef &out, int line)
{
    if (e == NULL || host == NULL)
        return false;

    switch (e->kind)
    {
    case thExprNode::CONST:
        out.kind = ExprRef::VALUE;
        out.value = e->value;
        return true;

    case thExprNode::NODEREF:
        out.kind = ExprRef::NODE;
        out.node = e->node;
        out.arg = e->arg;
        return true;

    case thExprNode::CHANREF:
        out.kind = ExprRef::KNOB;
        out.knob = sched->knob(e->name);

        /* parseExprFactor has already refused an undeclared knob against the
           line it is written on, so this is the belt rather than the braces
           -- but a false with nothing on stderr is the one way a .gen can
           decline to load and not say why. */
        if (out.knob == NULL)
        {
            error(line, "'@" + e->name + "' is not a declared knob");
            return false;
        }

        return true;

    case thExprNode::OP:
    case thExprNode::CALL:
        break;
    }

    /* Which plugin, how many args and what they are called. thExpr answers
       it for thSynthTree's desugar too, so an operator cannot come to mean
       one node in a .dsp and another in a .gen. */
    const char *spelling;
    const char *argname[3];
    int arity;

    if (!thExprPlugin(e, spelling, arity, argname))
    {
        if (e->kind == thExprNode::OP)
            error(line, "there is no node for that operator");
        else
            error(line, "'" + e->name + "' is not a function");

        return false;
    }

    /* Children first, so the numbering reads bottom up and a subexpression's
       node exists before the node that reads it. */
    ExprRef kid[3];

    for (int i = 0; i < arity; i++)
        if (!emitExpr(sched, host, e->kids[i], base, serial, kid[i], line))
            return false;

    char suffix[24];

    snprintf(suffix, sizeof(suffix), "#%d", ++serial);

    const std::string name = base + suffix;

    std::string why;

    if (!host->addNode(name, spelling, why))
    {
        error(line, why);
        return false;
    }

    for (int i = 0; i < arity; i++)
    {
        bool ok = true;

        switch (kid[i].kind)
        {
        case ExprRef::VALUE:
            ok = host->setValue(name, argname[i], kid[i].value, why);
            break;

        case ExprRef::NODE:
            ok = host->setWire(name, argname[i], kid[i].node, kid[i].arg,
                               why);
            break;

        case ExprRef::KNOB:
            ok = host->setKnob(name, argname[i], kid[i].knob, why);
            break;
        }

        if (!ok)
        {
            error(line, why);
            return false;
        }
    }

    out.kind = ExprRef::NODE;
    out.node = name;
    out.arg = "out";

    return true;
}

bool
thcGenLoader::parseNodeStage (thcScheduler *sched, size_t chain,
                              const std::string &chainName,
                              const Token &stageName, const Token &category,
                              const Token &plugName)
{
    thcChain *c = sched->chain(chain);

    if (c == NULL)
        return false;

    if (!c->nodes)
        c->nodes.reset(sched->newNodeHost());

    std::string why;

    /* `osc::simple' is `osc/simple' on disk, which is the same mapping
       the .dsp grammar makes -- one `::' becomes one `/'. */
    if (!c->nodes->addNode(stageName.text,
                           category.text + "/" + plugName.text, why))
    {
        error(category.line, "chain " + chainName + ", stage " +
              stageName.text + ": " + why);

        /* Step over the body rather than leaving the parser sitting on
           its `{'. One refused module should read as one refused
           module; returning here left parseChain looking at a brace it
           has no rule for, and it reported a second, wrong complaint
           about the chain before abandoning the rest of it. */
        skipStatement();

        return false;
    }

    if (!expectPunct('{'))
        return false;

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

        if (t.kind != Token::WORD)
        {
            error(t.line, "stage " + stageName.text +
                  ": expected an arg name");
            ok = false;
            skipToNextInBlock();
            continue;
        }

        Token argName = take();

        if (!expectPunct('='))
        {
            ok = false;
            skipToNextInBlock();
            continue;
        }

        /* One expression grammar for all four shapes a node arg can take.
         *
         * `freq = 0.05', `in0 = other->out' and `in1 = @depth' come back as
         * the leaf they are and take the same three calls they always did;
         * anything with an operator in it becomes the math:: nodes it stands
         * for, in this chain's own host. See GEN_FORMAT.md 5a: the file used
         * to have to write those nodes out, three lines at a time. */
        const int exprLine = peek().line;

        thExprNode *e = parseExpr(sched);

        if (e == NULL)
        {
            ok = false;
            skipToNextInBlock();
            continue;
        }

        ExprRef ref;
        int serial = 0;

        const bool built =
            emitExpr(sched, c->nodes.get(), e,
                     stageName.text + "." + argName.text, serial, ref,
                     exprLine);

        thExprFree(e);

        if (!built)
        {
            ok = false;
            skipToNextInBlock();
            continue;
        }

        bool bound = true;

        switch (ref.kind)
        {
        case ExprRef::VALUE:
            bound = c->nodes->setValue(stageName.text, argName.text,
                                       ref.value, why);
            break;

        case ExprRef::NODE:
            bound = c->nodes->setWire(stageName.text, argName.text, ref.node,
                                      ref.arg, why);
            break;

        case ExprRef::KNOB:
            /* `in1 = @depth;' -- the same knob a stage param binds and
               an instrument chanarg reads, one world further out.
               Leaving nodes out of the namespace phase 2 unified would
               have made an LFO's depth the one number in a piece that
               could not go on a slider. */
            bound = c->nodes->setKnob(stageName.text, argName.text, ref.knob,
                                      why);
            break;
        }

        if (!bound)
        {
            error(exprLine, why);
            ok = false;
        }

        if (!expectPunct(';'))
        {
            ok = false;
            skipToNextInBlock();
        }
    }

    if (!expectPunct(';'))
        ok = false;

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

    /* `stage lfo osc::simple { ... }' -- a DSP node, not a composer.
     *
     * The .dsp spelling, unchanged, which is the same call the arrow
     * makes: a person who has read a patch can read this line, and the
     * family is right there in it. UNIFICATION.md sketched `dsp::sine',
     * and the sketch is worse than what it sketched -- `simple' alone
     * does not say which of the plugin directories to look in, and the
     * family is exactly what has to be judged before the module is
     * allowed to run one sample at a time. A marker that hides the thing
     * being checked is not a marker.
     *
     * Still spelled `stage', because inside a chain everything is, and
     * §0 of the plan is emphatic that the day a `node' can appear where
     * a `stage' goes the wrong intuitions about order and lifetime come
     * with it. What tells the two apart is the category, as it always
     * was: `gen' and `xform' are the two ends of the composer ABI, and
     * anything else is a plugin family from the other world.
     *
     * What a node is *not* is part of the event flow. It neither ticks
     * nor receives; it holds a value the stages around it can read. So
     * it goes to a different host, and comes back through parseParam's
     * arrow rather than through a sink. */
    if (category.text != "gen" && category.text != "xform")
        return parseNodeStage(sched, chain, chainName, stageName,
                              category, plugName);

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
    /* Unreachable: everything that is not gen or xform went to the node
       path above, which is where an unknown family is reported -- with
       the list of families that do work, which is the more useful half
       of the answer. */

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

        if (!parseParam(sched, chain, stage, stageName.text))
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
thcGenLoader::parseParam (thcScheduler *sched, size_t chainIndex,
                          thcStage *stage, const std::string &stageName)
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

    /* Arithmetic, before anything else looks at the value.
     *
     * A lookahead rather than one parse for every shape, because a param's
     * value may be a note list, a preset name or an instrument set, none of
     * which an expression grammar has any business reading. So every
     * existing spelling stays on the branch it was already on, and the new
     * one engages only where an operator says so. */
    if (aheadIsExpression())
    {
        const int line = peek().line;

        if (pi->type == THC_PARAM_NOTESET || pi->type == THC_PARAM_STRING ||
            pi->type == THC_PARAM_PRESET || pi->type == THC_PARAM_INSTRSET)
        {
            error(line, "'" + pname.text +
                  "' is not numeric; arithmetic cannot drive it");
            return false;
        }

        thcChain *c = sched->chain(chainIndex);

        if (c == NULL)
        {
            error(line, "'" + pname.text + "': no chain to build this in");
            return false;
        }

        /* On demand, exactly as a `stage lfo osc::simple' would: arithmetic
           on a param *is* a dsp stage, written on the line that uses it
           rather than three lines above. A chain whose only nodes are
           these gets its host here and nowhere else. */
        if (!c->nodes)
            c->nodes.reset(sched->newNodeHost());

        thExprNode *e = parseExpr(sched);

        if (e == NULL)
            return false;

        ExprRef ref;
        int serial = 0;

        const bool built =
            emitExpr(sched, c->nodes.get(), e,
                     stageName + "." + pname.text, serial, ref, line);

        thExprFree(e);

        if (!built)
            return false;

        switch (ref.kind)
        {
        case ExprRef::VALUE:
            /* Nothing left but a number, and a number on a duration needs a
               unit -- the same rule a bare literal meets, since folding is
               what makes this one a bare literal. An expression with a
               signal in it carries no unit and is not asked for one, which
               is what a knob and a node on a duration have always done. */
            if (pi->isDuration())
            {
                error(line, "'" + pname.text +
                      "' is a duration; write a unit (s, ms or beats)");
                return false;
            }

            stage->params.set(idx, ref.value);
            break;

        case ExprRef::KNOB:
            sched->bindKnob(stage, idx, ref.knob);
            break;

        case ExprRef::NODE:
        {
            /* Parked like any other `node->arg' on a param: the host cannot
               resolve the name until the chain has been read to its end. */
            PendingNodeBind b;

            b.chain = chainIndex;
            b.stage = stage;
            b.param = idx;
            b.node  = ref.node;
            b.arg   = ref.arg;
            b.line  = line;

            pendingNodeBinds_.push_back(b);
            break;
        }
        }

        return expectPunct(';');
    }

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

            if (pi->type == THC_PARAM_PRESET)
            {
                error(num.line, "'" + pname.text +
                      "' wants a preset name, not a number");
                return false;
            }

            if (pi->type == THC_PARAM_INSTRSET)
            {
                error(num.line, "'" + pname.text +
                      "' wants instrument names, not a number");
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

        if (pi->type == THC_PARAM_NOTESET || pi->type == THC_PARAM_STRING ||
            pi->type == THC_PARAM_PRESET || pi->type == THC_PARAM_INSTRSET)
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

            /* One pitch, and a rest is not one: a `.' here would set the
               param to -1 and a plugin would read a note below the bottom
               of the keyboard. Said in its own words rather than folded
               into "wants one note name", because a `.' is good spelling
               in every note *list* in the file and being told it is not a
               note name would read as a lie. */
            if (resolved[0] < 0)
            {
                error(str.line, "'" + pname.text + "' wants a note, and a "
                      "rest is not one");
                return false;
            }

            stage->params.set(idx, resolved[0]);
        }
        else if (pi->type == THC_PARAM_STRING)
            stage->params.setString(idx, str.text);
        else if (pi->type == THC_PARAM_PRESET)
        {
            /* No quoted literal, unlike a NOTESET. A one-off pitch pool
               is a reasonable thing to write inline; a one-off timbre
               vector spelled as text is a preset that cannot be morphed
               towards, bred from or saved under a name, which is the
               whole reason the noun exists. Say so rather than accept
               it. */
            error(str.line, "'" + pname.text +
                  "' wants a preset name; declare it with `preset'");
            return false;
        }
        else if (pi->type == THC_PARAM_INSTRSET)
        {
            /* `instruments = "voice,bell,glass";' -- the list form. A
               bare word above names one; this names several, and it is
               quoted for the flat reason that `,' is not punctuation
               this language has. Every name is checked here, so a
               composer receives a list it can trust and a typo is an
               error against the line that made it rather than a swap
               that silently does nothing a minute in. */
            std::string list;
            std::string one;

            for (size_t i = 0; i <= str.text.size(); i++)
            {
                const char ch = i < str.text.size() ? str.text[i] : ',';

                /* Newlines and returns separate too. A quoted string can
                   hold one, and a name with a \r stuck to it is a name
                   nothing declares -- an error about a typo the author
                   cannot see. */
                if (ch != ',' && ch != ' ' && ch != '\t' &&
                    ch != '\n' && ch != '\r')
                {
                    one += ch;
                    continue;
                }

                if (one.empty())
                    continue;

                if (instruments_.find(one) == instruments_.end())
                {
                    error(str.line, "no instrument called '" + one +
                          "' has been declared");
                    return false;
                }

                /* Twice in the list is a typo. It is not harmful --
                   the swap service answers a swap to what is already
                   there by doing nothing -- but a round-robin that
                   dwells two turns on one instrument is not what
                   anybody wrote down. */
                if (("," + list + ",").find("," + one + ",") !=
                    std::string::npos)
                {
                    error(str.line, "'" + one + "' is named twice");
                    return false;
                }

                if (!list.empty())
                    list += ",";

                list += one;
                one.clear();
            }

            if (list.empty())
            {
                error(str.line, "'" + pname.text + "' names no instruments");
                return false;
            }

            stage->params.setString(idx, list);
        }
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
        /* A bare word as a value names a declared object: a scale for a
           note set, a preset for a chanarg vector. Which one is decided
           by the param, not by the word, so a typo is reported against
           the kind of thing the plugin actually asked for. */
        Token ref = take();

        /* Unless an arrow follows it, in which case it names a node.
           `step = lfo->out' -- the composer-world ARG_NODE, which the
           v2 format refused to invent a syntax for until there were
           nodes with defined evaluation semantics to bind. There are
           now, and the syntax was never going to be anything but the
           one .dsp already uses. */
        if (peek().kind == Token::PUNCT && peek().text == "->")
        {
            take();

            if (peek().kind != Token::WORD)
            {
                error(peek().line, "expected an arg name after '" +
                      ref.text + "->'");
                return false;
            }

            Token fromArg = take();

            if (pi->type == THC_PARAM_NOTESET ||
                pi->type == THC_PARAM_STRING ||
                pi->type == THC_PARAM_PRESET ||
                pi->type == THC_PARAM_INSTRSET)
            {
                error(ref.line, "'" + pname.text +
                      "' is not numeric; a node cannot drive it");
                return false;
            }

            /* Parked: the host cannot resolve the name until the chain
               has been read to its end. */
            PendingNodeBind b;

            b.chain = chainIndex;
            b.stage = stage;
            b.param = idx;
            b.node  = ref.text;
            b.arg   = fromArg.text;
            b.line  = ref.line;

            pendingNodeBinds_.push_back(b);

            return expectPunct(';');
        }

        if (pi->type == THC_PARAM_PRESET)
        {
            std::map<std::string, Preset>::const_iterator p =
                presets_.find(ref.text);

            if (p == presets_.end())
            {
                error(ref.line, "no preset called '" + ref.text +
                      "' has been declared");
                return false;
            }

            stage->params.setString(idx, presetToString(p->second));

            return expectPunct(';');
        }

        /* `instruments = voice;' -- one of the piece's own, named the
           way a scale or a preset is. The quoted form below is how more
           than one is written; this is the same identifier-or-literal
           bargain a note set already offers, for the same reason. */
        if (pi->type == THC_PARAM_INSTRSET)
        {
            if (instruments_.find(ref.text) == instruments_.end())
            {
                error(ref.line, "no instrument called '" + ref.text +
                      "' has been declared");
                return false;
            }

            stage->params.setString(idx, ref.text);

            return expectPunct(';');
        }

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
    std::string instrument;
    int instrumentLine = 0;

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
            (t.text != "channel" && t.text != "chanarg" &&
             t.text != "instrument"))
        {
            error(t.line, "a sink says 'instrument = name' or "
                  "'channel = N', and optionally 'chanarg = \"name\"' "
                  "or 'chanarg = \"*\"'");
            return false;
        }

        Token key = take();

        if (!expectPunct('='))
            return false;

        if (key.text == "instrument")
        {
            const Token &v = peek();

            if (v.kind != Token::WORD)
            {
                error(v.line, "instrument wants the name of a declared "
                      "instrument");
                return false;
            }

            Token ref = take();

            if (instruments_.find(ref.text) == instruments_.end())
            {
                error(ref.line, "no instrument called '" + ref.text +
                      "' has been declared");
                return false;
            }

            instrument = ref.text;
            instrumentLine = ref.line;
        }
        else if (key.text == "channel")
        {
            const Token &v = peek();

            if (v.kind != Token::NUMBER)
            {
                error(v.line, "channel wants a number");
                return false;
            }

            Token num = take();

            /* 1-16, the way every other place a person sees a channel in
               this program spells one: the main window's patch tabs and
               the Keyboard window's spinner have always counted from one,
               and so does every sequencer anyone has used. The wire and
               the engine count from zero, and the conversion belongs
               here, at the file boundary, exactly as the note-name
               parser does.
             *
               `channel = 0' was the whole of the old range's bottom and
               is now an error rather than channel 1, which is the one
               case where a file written for the old numbering can be
               told apart from one written for this. It says so. */
            if (num.num == 0)
            {
                error(num.line, "channel is 1-16 now, counting the way the "
                      "patch tabs do; there is no channel 0");
                return false;
            }

            if (num.num < 1 || num.num > 16 ||
                num.num != (double)(int)num.num)
            {
                error(num.line, "channel is a whole number, 1-16");
                return false;
            }

            channel = (int)num.num - 1;
        }
        else
        {
            const Token &v = peek();

            if (v.kind != Token::STRING)
            {
                error(v.line, "chanarg wants a quoted name");
                return false;
            }

            Token argTok = take();

            chanarg = argTok.text;

            /* `*', an identifier, or an identifier behind the `fx.'
               that names the channel's effect rather than its
               instrument. A chanarg is declared in a .dsp as `@name', so
               anything that could not be written there cannot be
               delivered to either -- and a sink pointed at "cut off"
               would otherwise fail silently at delivery time, which is a
               long way from the typo.
             *
               The prefix is TH_EFFECT_PREFIX, the same spelling a
               .patch line and a MIDI binding use, and the same one an
               `effect' block's values are already stored under. It is
               the only punctuation allowed in here: `fx.' is something
               the engine puts in front of a name rather than part of
               one, which is why stripping it and checking what is left
               is the whole rule.
             *
               `fx.*' is not a form. A `*' sink keeps whatever name the
               event arrived with -- see thcSink::namesItsOwn -- so there
               is no name here for the prefix to go in front of, and a
               composer that wants to reach an effect writes the prefix
               on the event. */
            const size_t plen = strlen(TH_EFFECT_PREFIX);
            const bool prefixed =
                chanarg.compare(0, plen, TH_EFFECT_PREFIX) == 0;
            const std::string bare =
                prefixed ? chanarg.substr(plen) : chanarg;

            bool ok = chanarg == "*";

            if (!ok && !bare.empty() &&
                ((bare[0] >= 'a' && bare[0] <= 'z') ||
                 (bare[0] >= 'A' && bare[0] <= 'Z')))
            {
                ok = true;

                for (size_t i = 1; i < bare.size(); i++)
                {
                    const char c = bare[i];

                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_'))
                        ok = false;
                }
            }

            if (!ok)
            {
                error(argTok.line, "'" + chanarg + "' is not a chanarg name; "
                      "write the name a patch declares, \"" +
                      TH_EFFECT_PREFIX + "\" and the name its effect "
                      "declares, or \"*\" to let each event name its own");
                return false;
            }
        }

        if (!expectPunct(';'))
            return false;
    }

    if (!instrument.empty() && channel >= 0)
    {
        /* The two spellings answer the same question, so a sink using
           both is a sink whose author changed their mind halfway and
           left the other half in. Guessing which half is current is not
           this loader's job. */
        error(instrumentLine, "sink names both an instrument and a "
              "channel; it is one or the other");
        return false;
    }

    if (instrument.empty() && channel < 0)
    {
        /* -1 is "never set", which the range check above makes
           unreachable any other way. */
        error(peek().line, "sink has no instrument and no channel");
        return false;
    }

    /* An instrument sink's channel is not known yet -- see
       allocateChannels for why it cannot be. The sink goes in with a
       number that could not be mistaken for a real one, and the second
       pass fills it. */
    sched->addSink(chain, instrument.empty() ? channel : -1, chanarg);

    if (instrument.empty())
        claimedChannels_.push_back(channel);
    else
    {
        thcChain *c = sched->chain(chain);
        PendingSink p;

        p.chain      = chain;
        p.sink       = c != NULL && !c->sinks.empty() ? c->sinks.size() - 1 : 0;
        p.instrument = instrument;
        p.line       = instrumentLine;

        pendingSinks_.push_back(p);
    }

    return expectPunct(';');
}

/* Every `param = node->arg' on a composer stage, resolved against the
 * chain's built node host.
 *
 * After the parse, like the channel allocation above it and for a
 * related reason: the host cannot answer what a node's output arg is
 * until its tree is built, and its tree cannot be built until the chain
 * has been read. What is resolved is a *pointer* to the output buffer,
 * which the param store then reads at the moment the composer asks --
 * so the value a stage sees is the value the node holds now, not the
 * one it held when the file loaded.
 */
void
thcGenLoader::bindNodes (thcScheduler *sched)
{
    for (size_t i = 0; i < pendingNodeBinds_.size(); i++)
    {
        const PendingNodeBind &b = pendingNodeBinds_[i];
        thcChain *c = sched->chain(b.chain);

        if (c == NULL || b.stage == NULL)
            continue;

        if (!c->nodes)
        {
            error(b.line, "'" + b.node + "->" + b.arg + "': this chain has "
                  "no dsp stages in it");
            continue;
        }

        std::string why;
        thArg *out = c->nodes->output(b.node, b.arg, why);

        if (out == NULL)
        {
            error(b.line, why);
            continue;
        }

        b.stage->params.bindNode(b.param, out);

        /* Announced, for the reason thcScheduler::bindKnob announces a
           knob: a stage is created before it is bound, so a module that
           caches its params never heard that this one now reads a node
           and went on running against the registered default. Unlike a
           knob there is no later signal to fall back on -- a node's
           output is read, not pushed -- so this notification is the
           only one the module will get about the binding existing. */
        b.stage->params.notifyChanged(b.param);
    }
}

/* ---- channels ---------------------------------------------------------- */

/* Every instrument gets a channel, and every sink that named one gets
 * the number.
 *
 * Three facts decide the shape of this. An instrument's channel is an
 * implementation detail the author never sees, so any assignment will do
 * as long as it is the *same* one every time the file is read -- a piece
 * whose instruments landed somewhere different on each load would break
 * every patch tab, every saved mixer setting and the piano roll's
 * per-channel colors, all at once. `channel = N' is still in the
 * language for driving a patch this piece does not own, so a number a
 * sink claimed outright must not be handed to an instrument underneath
 * it. And a channel that already has somebody else's patch on it is not
 * free either -- opening a piece must not quietly replace an instrument
 * the person loaded by hand, which only the host can answer and which
 * thcScheduler::channelTaken is how it does.
 *
 * Hence: lowest free channel, instruments in declaration order, claimed
 * and occupied numbers skipped. Deterministic given the same rack, and
 * it puts the first instrument on channel 1 where a person looking for
 * it would look first.
 */
bool
thcGenLoader::allocateChannels (thcScheduler *sched)
{
    bool taken[16];

    for (int i = 0; i < 16; i++)
        taken[i] = sched->channelTaken(i);

    for (size_t i = 0; i < claimedChannels_.size(); i++)
        if (claimedChannels_[i] >= 0 && claimedChannels_[i] < 16)
            taken[claimedChannels_[i]] = true;

    for (size_t i = 0; i < sched->instruments().size(); i++)
    {
        thcInstrument *inst = sched->instrument(i);
        int at = -1;

        for (int c = 0; c < 16 && at < 0; c++)
            if (!taken[c])
                at = c;

        if (at < 0)
        {
            error(i < instrumentLines_.size() ? instrumentLines_[i] : 0,
                  "there is no free channel left for instrument '" +
                  inst->name + "'; sixteen is all there are");
            return false;
        }

        taken[at] = true;
        inst->channel = at;
    }

    /* And the effects that named a side: the same turn from a name into a
       number the sinks below get, and it has to happen here for the same
       reason -- the instrument being listened to may be the one that has
       just been given a channel.

       The name was checked against the declared instruments when it was
       read, so a miss here is this loader having a bug rather than the file
       having an error. */
    for (size_t i = 0; i < sched->instruments().size(); i++)
    {
        thcInstrument *inst = sched->instrument(i);

        if (inst == NULL || inst->side.empty())
            continue;

        const std::map<std::string, size_t>::const_iterator at =
            instruments_.find(inst->side);
        const thcInstrument *of =
            at != instruments_.end() ? sched->instrument(at->second) : NULL;

        if (of == NULL)
            continue;

        inst->sideChannel = of->channel;
    }

    for (size_t i = 0; i < pendingSinks_.size(); i++)
    {
        const PendingSink &p = pendingSinks_[i];
        const thcInstrument *inst = sched->instrument(p.instrument);
        thcChain *c = sched->chain(p.chain);

        /* Both were checked when they were read; if either is gone now
           the loader has a bug rather than the file having an error. */
        if (inst == NULL || c == NULL || p.sink >= c->sinks.size())
            continue;

        c->sinks[p.sink].channel = inst->channel;
    }

    return true;
}
