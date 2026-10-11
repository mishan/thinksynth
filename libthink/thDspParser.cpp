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

/* The .dsp grammar, by recursive descent over the shared token stream.
 *
 * It was bison, and the language did not need it: every statement is told
 * apart by its first token or two, and the one real ambiguity -- `%' as a
 * modulo or a percentage -- is a single token of lookahead. What bison cost
 * was a generated parser .gen could not call into, which is what an inline
 * graph inside a piece needs, and a build dependency every platform had to
 * be talked into. */

#include "config.h"

#include <stdio.h>

#include <string>
#include <vector>

#include "think.h"
#include "parser.h"
#include "thUnits.h"
#include "thArrays.h"

namespace {

/* What the grammar sees: the shared lexer's tokens with .dsp's vocabulary
   applied. Words become keywords here and nowhere else, because which
   words mean something is a question about a language, and the lexer
   serves two (thLexer.h). */
enum Kind
{
    END, NUMBER, NIL, STRING, WORD,
    NODE, IO, NAME, DESC, AUTHOR, CAT, MS, UNIT,
    PUNCT
};

struct Tok
{
    Kind               kind;
    float              num;
    const char        *units;   /* "ms" on th_sample, the unit on a UNIT */
    const thLexToken  *src;
};

/* A value as the expression rules pass it up: a number with the unit it
   was written in, or a tree once a leaf is a signal. The unit is the
   author's and is folded at load time by thSynthTree::foldUnits, which
   knows the rate. */
struct Value
{
    float        num;
    const char  *units;
    thExprNode  *expr;

    Value (void) : num(0), units(NULL), expr(NULL) { }
};

Tok
classify (const std::vector<thLexToken> &tokens, size_t i)
{
    const thLexToken &t = tokens[i];
    Tok k = { PUNCT, 0, NULL, &t };

    switch (t.kind)
    {
    case thLexToken::END:
    case thLexToken::ERROR:
        k.kind = END;
        return k;

    case thLexToken::NUMBER:
        k.kind = NUMBER;
        k.num = (float)t.num;
        return k;

    case thLexToken::STRING:
        k.kind = STRING;
        return k;

    case thLexToken::PUNCT:
        return k;

    case thLexToken::WORD:
        break;
    }

    const std::string &w = t.text;

    k.kind = NUMBER;

    /* The named constants: numbers the engine supplies, which is why they
       are keywords rather than something a .dsp could shadow. */
    if (w == "th_max")      { k.num = TH_MAX;     return k; }
    if (w == "th_min")      { k.num = TH_MIN;     return k; }
    if (w == "th_range")    { k.num = TH_RANGE;   return k; }
    if (w == "th_midimax")  { k.num = MIDIVALMAX; return k; }
    /* A second of samples at whatever rate the synth runs at, carried as
       `1000 ms' so it folds at load time with every other `ms'. */
    if (w == "th_sample")   { k.num = 1000; k.units = "ms"; return k; }

    /* Hard keywords: no .dsp may name a node or an arg any of these. .gen's
       are contextual and reserve nothing -- docs/GEN_FORMAT.md 7a says why
       the two languages differ here. */
    k.kind = WORD;

    if      (w == "nil")         k.kind = NIL;
    else if (w == "node")        k.kind = NODE;
    else if (w == "io")          k.kind = IO;
    else if (w == "name")        k.kind = NAME;
    else if (w == "description") k.kind = DESC;
    else if (w == "category")    k.kind = CAT;
    else if (w == "author")      k.kind = AUTHOR;
    else if (w == "ms")          k.kind = MS;
    /* The other units are words only straight after a number, so `s' is
       still the sustain every envelope has, and `beats' a name a node may
       take. */
    else if (i > 0 && tokens[i - 1].kind == thLexToken::NUMBER &&
             (k.units = thUnitWord(w)) != NULL)
        k.kind = UNIT;

    return k;
}

class Parser
{
public:
    Parser (thSynth *synth, thSynthTree *tree,
            const std::vector<thLexToken> &tokens)
        : synth_(synth), tree_(tree), node_(new thNode("newnode", NULL)),
          pos_(0), depth_(0)
    {
        for (size_t i = 0; i < tokens.size(); i++)
            toks_.push_back(classify(tokens, i));
    }

    ~Parser (void) { delete node_; }

    bool statements (void);

private:
    thSynth            *synth_;
    thSynthTree        *tree_;
    thNode             *node_;      /* the node whose body is being read  */
    std::vector<Tok>    toks_;
    size_t              pos_;
    int                 depth_;     /* factor nesting, against the stack  */

    const Tok &peek (size_t ahead = 0) const
    {
        size_t i = pos_ + ahead;
        return toks_[i < toks_.size() ? i : toks_.size() - 1];
    }

    const Tok &take (void)
    {
        const Tok &t = toks_[pos_];

        if (t.kind != END)
            pos_++;

        return t;
    }

    bool isPunct (const Tok &t, const char *p) const
    {
        return t.kind == PUNCT && t.src->text == p;
    }

    bool accept (const char *p)
    {
        if (!isPunct(peek(), p))
            return false;

        take();
        return true;
    }

    void error (const Tok &at, const std::string &msg) const
    {
        fprintf(stderr, "line %d: error: %s\n", at.src->line, msg.c_str());
    }

    /* `expected X', naming what was there instead. */
    bool expected (const char *what)
    {
        const Tok &t = peek();
        std::string found = t.kind == END ? "the end of the file"
                          : t.kind == STRING ? "\"" + t.src->text + "\""
                          : "'" + t.src->text + "'";

        /* What is missing belongs to the line before whatever was found
           instead: a `;' forgotten on line 2 is not line 5's mistake, nor
           the end of the file's. */
        const Tok &at = pos_ > 0 && (t.kind == END ||
                                     t.src->line > toks_[pos_ - 1].src->line)
                      ? toks_[pos_ - 1] : t;

        error(at, std::string("expected ") + what + ", found " + found);
        return false;
    }

    bool expect (const char *p)
    {
        if (accept(p))
            return true;

        return expected((std::string("'") + p + "'").c_str());
    }

    bool word (std::string &out, const char *what)
    {
        if (peek().kind != WORD)
            return expected(what);

        out = take().src->text;
        return true;
    }

    bool statement (void);
    bool nodeStatement (void);
    bool control (void);
    bool assignment (void);

    bool expression (Value &out);
    bool term (Value &out);
    bool unitFactor (Value &out);
    bool factor (Value &out);
    bool startsFactor (const Tok &t) const;

    bool noUnits (const Tok &at, const char *a, const char *b);
    bool deepEnough (const Tok &at, Value &out);
    bool arith (const Tok &at, Value &out, int op, Value a, Value b);
    bool call (const Tok &at, Value &out, const std::string &name,
               std::vector<Value> &argv);
};

bool
Parser::statements (void)
{
    while (peek().kind != END)
    {
        const bool wasNode = peek().kind == NODE;

        if (!statement())
            return false;

        if (wasNode && peek().kind == NODE)
        {
            error(peek(), "missing semicolon after node");
            return false;
        }

        if (!expect(";"))
            return false;
    }

    return true;
}

bool
Parser::statement (void)
{
    const Tok &t = peek();

    switch (t.kind)
    {
    case NODE:
        return nodeStatement();

    case IO:
    {
        take();

        std::string name;

        if (!word(name, "the io node's name"))
            return false;

        tree_->setIONode(name);
        return true;
    }

    case NAME:
    case DESC:
    case CAT:
    case AUTHOR:
    {
        take();

        if (peek().kind != STRING)
            return expected("a quoted string");

        const std::string &s = take().src->text;

        /* The info strings have been readable as chanargs since long
           before anything else could read them, and a field that appeared
           in one of those places and not the other would be a field half
           the tree cannot see. */
        const char *argName = t.kind == NAME ? "name"
                            : t.kind == DESC ? "desc"
                            : t.kind == CAT  ? "category" : "author";

        thArg *info = new thArg(argName, NULL, 0);
        info->setComment(s);
        tree_->setChanArg(info);

        /* The tree's name is what treelist_ is keyed on. */
        if (t.kind == NAME)
            tree_->setName(s);
        else if (t.kind == DESC)
            tree_->setDesc(s);
        else if (t.kind == CAT)
            tree_->setCategory(s);

        return true;
    }

    default:
        break;
    }

    /* `@cut = 0.5' declares a control; `@cut' alone is a value. */
    if (isPunct(t, "@") && peek(1).kind == WORD &&
        (isPunct(peek(2), "=") || isPunct(peek(2), ".")))
        return control();

    if (!startsFactor(t))
        return expected("a statement");

    /* A bare expression statement, which only ever printed its value. A
       signal has no value to print at parse time and no arg to drive, so
       there is nothing to say about one. */
    Value v;

    if (!expression(v))
        return false;

    if (v.expr == NULL)
        printf("%f\n", v.num);

    thExprFree(v.expr);

    return true;
}

bool
Parser::nodeStatement (void)
{
    take();

    std::string name;

    if (!word(name, "a node name"))
        return false;

    /* `osc::simple' is the plugin at `osc/simple'. */
    std::string plugName;
    const Tok &plugAt = peek();

    if (plugAt.kind == WORD)
    {
        plugName = take().src->text;

        do
        {
            std::string part;

            if (!expect("::") || !word(part, "a plugin name after '::'"))
                return false;

            plugName += "/" + part;
        }
        while (isPunct(peek(), "::"));
    }

    if (!expect("{"))
        return false;

    while (!isPunct(peek(), "}"))
        if (!assignment() || !expect(";"))
            return false;

    take();

    /* The body is read before the plugin is looked up, as it always was:
       an error inside a node is reported before a plugin that is missing. */
    thPlugin *plug = NULL;

    if (!plugName.empty())
    {
        plug = synth_->getPluginManager()->getOrLoadPlugin(plugName);

        /* A library does not take its host down over one .dsp naming a
           plugin that will not load; the parse fails and loadTree discards
           the tree. */
        if (plug == NULL)
        {
            error(plugAt, "could not load plugin '" + plugName +
                  "' required by node '" + name + "'");
            return false;
        }
    }

    node_->setPlugin(plug);
    node_->setName(name);

    tree_->newNode(node_, true);
    node_ = new thNode("newnode", NULL);

    return true;
}

/* `@cut = 0.5', `@cut.max = 2000 ms', `@cut.label = "Cutoff"'. */
bool
Parser::control (void)
{
    take();

    const std::string name = take().src->text;

    if (accept("="))
    {
        const Tok &at = peek();
        Value v;

        if (!expression(v))
            return false;

        /* A control is a constant the GUI writes into, and nothing drives
           one: thMidiChan copies the declared value into every voice and a
           slider overwrites it. An expression here would be a value with
           two authors. */
        if (v.expr)
        {
            error(at, "a control cannot be driven by an expression");
            thExprFree(v.expr);
            return false;
        }

        thArg *chanarg = new thArg(name, v.num);

        /* An explicit `@a.units = "Hz"' later still overrides this -- the
           author knows better than the fold does. */
        if (v.units)
            chanarg->setUnits(v.units);

        tree_->setChanArg(chanarg);

        /* After setChanArg, which owns the arg now: a second `@a' replaces
           the first and drops any fold still aimed at it. */
        if (v.units)
            tree_->deferUnitFold(chanarg, thUnitFold::VALUE, v.num, v.units);

        return true;
    }

    take();

    std::string field;

    if (!word(field, "a field name after '.'") || !expect("="))
        return false;

    thArg *chanarg = tree_->getChanArg(name);

    if (peek().kind == STRING)
    {
        const std::string &s = take().src->text;

        if (chanarg == NULL)
            printf("ERROR:  '@%s.%s' set before '@%s' was declared; "
                   "ignoring\n", name.c_str(), field.c_str(), name.c_str());
        else if (field == "label")
            chanarg->setLabel(s);
        else if (field == "units")
            chanarg->setUnits(s);
        /* Presentation only. An editor draws a group's controls together;
           the engine never looks at it. */
        else if (field == "group")
            chanarg->setGroup(s);
        /* `@x.values = "Sine,Sawtooth,Square"' -- names for the whole
           numbers this control selects between, which implies `.step = 1'
           and a range of 0..count-1. The author's word, so typeChanArgs()
           will not overwrite it. */
        else if (field == "values")
            chanarg->setValueNames(s, true);
        else
            printf("ERROR:  Invalid arg parameter '%s <string>'\n",
                   field.c_str());

        return true;
    }

    const Tok &at = peek();
    Value v;

    if (!expression(v))
        return false;

    if (v.expr)
    {
        error(at, "a control's range cannot be driven by an expression");
        thExprFree(v.expr);
        return false;
    }

    if (chanarg == NULL)
    {
        printf("ERROR:  '@%s.%s' set before '@%s' was declared; ignoring\n",
               name.c_str(), field.c_str(), name.c_str());
    }
    else if (field == "min" || field == "max")
    {
        if (field == "min")
            chanarg->setMin(v.num);
        else
            chanarg->setMax(v.num);

        /* `@a.max = 2000ms' says as much about the arg as `@a = 5 ms'
           does, and some patches give the unit only on the range. */
        if (v.units && chanarg->units().empty())
            chanarg->setUnits(v.units);

        /* Parked per site, not per arg. A file may write the value in
           milliseconds and the range in samples, and folding the whole arg
           by its unit would convert a number that was already converted. */
        if (v.units)
            tree_->deferUnitFold(chanarg, field == "min" ? thUnitFold::MIN
                                                         : thUnitFold::MAX,
                                 v.num, v.units);
    }
    else if (field == "widget")
    {
        chanarg->setWidgetType((thArg::WidgetType)v.num);
    }
    else if (field == "step")
    {
        /* `true' marks it as the author's, so typeChanArgs() leaves it
           alone: that is what lets `@x.step = 0' mean "continuous, and I
           mean it" rather than having said nothing. */
        chanarg->setStep(v.num, true);

        if (v.units)
            tree_->deferUnitFold(chanarg, thUnitFold::STEP, v.num, v.units);
    }
    else
        printf("ERROR:  Invalid arg parameter '%s <numeric>'\n",
               field.c_str());

    return true;
}

/* One line of a node's body. Every right-hand side but a quoted string goes
 * through the expression rules, and this sorts out what came back: a
 * number, a `node->arg', an `@name', or arithmetic over signals that
 * desugarExprs turns into math:: nodes. */
bool
Parser::assignment (void)
{
    std::string name;

    if (!word(name, "an arg name") || !expect("="))
        return false;

    tree_->dropExpr(node_, name);

    /* A quoted name on a node arg -- osc::sample's `file' -- which cannot
       be swept by a slider, stored in a preset or reached by a MIDI
       controller, so the control machinery has nothing to offer it. */
    if (peek().kind == STRING)
    {
        node_->setTextArg(name, take().src->text)->setIndex(-1);
        return true;
    }

    Value v;

    if (!expression(v))
        return false;

    if (v.expr == NULL)
    {
        thArg *arg = node_->setArg(name, v.num);

        arg->setIndex(-1);

        /* The arg holds the author's number until foldUnits runs. */
        if (v.units)
        {
            arg->setUnits(v.units);
            tree_->deferUnitFold(arg, thUnitFold::VALUE, v.num, v.units,
                                 node_);
        }
    }
    else if (v.expr->kind == thExprNode::NODEREF)
    {
        node_->setArg(name, v.expr->node, v.expr->arg)->setIndex(-1);
        thExprFree(v.expr);
    }
    else if (v.expr->kind == thExprNode::CHANREF)
    {
        node_->setArg(name, v.expr->name)->setIndex(-1);
        thExprFree(v.expr);
    }
    else
    {
        /* Against the node rather than its name, which is read after its
           body. See thPendingExpr. */
        tree_->deferExpr(node_, name, v.expr);
    }

    return true;
}

/* ---- expressions --------------------------------------------------------
 *
 * expression := term (('+' | '-') term)*
 * term       := unit (('*' | '/') unit)*
 * unit       := factor ['%' factor | '%' | 'ms' | UNIT]
 * factor     := '(' expression ')' | NUMBER | 'nil' | '-' factor
 *             | WORD '->' WORD | '@' WORD | WORD '(' expression{1,3} ')'
 *
 * Left-associative, so `a - b + c' is `(a - b) + c', and a unary minus
 * binds to its operand: `-0.4 + 0.1' is -0.3, as .gen reads it too. */

bool
Parser::startsFactor (const Tok &t) const
{
    return t.kind == NUMBER || t.kind == NIL || t.kind == WORD ||
           isPunct(t, "(") || isPunct(t, "-") || isPunct(t, "@");
}

bool
Parser::expression (Value &out)
{
    if (!term(out))
        return false;

    while (isPunct(peek(), "+") || isPunct(peek(), "-"))
    {
        const Tok &op = take();
        Value right;

        if (!term(right))
        {
            thExprFree(out.expr);
            return false;
        }

        /* A unit in a sum is refused rather than guessed: `5 ms + 3' has
           no unit this grammar can name, and dropping it would make it 8
           samples. */
        if (!arith(op, out, op.src->text[0], out, right))
            return false;
    }

    return true;
}

bool
Parser::term (Value &out)
{
    if (!unitFactor(out))
        return false;

    while (isPunct(peek(), "*") || isPunct(peek(), "/"))
    {
        const Tok &op = take();
        Value right;

        if (!unitFactor(right))
        {
            thExprFree(out.expr);
            return false;
        }

        if (!arith(op, out, op.src->text[0], out, right))
            return false;
    }

    return true;
}

/* A factor with its unit, or a modulo, which binds as tightly. `%' is also
   the percentage, and only the token after it says which: one that can
   begin a factor makes a modulo, except `-', so that `50% - 3' stays the
   percentage minus three. */
bool
Parser::unitFactor (Value &out)
{
    if (!factor(out))
        return false;

    const Tok &t = peek();
    const char *units;

    if (isPunct(t, "%"))
    {
        take();

        if (startsFactor(peek()) && !isPunct(peek(), "-"))
        {
            Value right;

            if (!factor(right))
            {
                thExprFree(out.expr);
                return false;
            }

            return arith(t, out, '%', out, right);
        }

        units = "%";
    }
    else if (t.kind == MS)
    {
        take();
        units = "ms";
    }
    else if (t.kind == UNIT)
    {
        take();
        units = t.units;
    }
    else
        return true;

    /* The literal, not the fold: whoever stores the value parks a fold for
       thSynthTree::foldUnits, which knows the rate. */
    if (out.expr)
    {
        error(t, "a unit cannot be written on a signal");
        thExprFree(out.expr);
        return false;
    }

    /* `th_sample ms' among them: th_sample is `1000 ms' already. */
    if (out.units)
    {
        error(t, "a value with a unit cannot take another");
        return false;
    }

    out.units = units;

    return true;
}

bool
Parser::factor (Value &out)
{
    struct Depth
    {
        int &n;
        Depth (int &counter) : n(counter) { n++; }
        ~Depth (void) { n--; }
    } depth(depth_);

    const Tok &t = peek();

    out = Value();

    if (depth_ > TH_EXPR_DEPTH_MAX)
    {
        error(t, "this expression is nested too deeply");
        return false;
    }

    if (accept("("))
    {
        if (!expression(out))
            return false;

        if (!expect(")"))
        {
            thExprFree(out.expr);
            out.expr = NULL;
            return false;
        }

        return true;
    }

    if (t.kind == NUMBER)
    {
        take();
        out.num = t.num;
        out.units = t.units;
        return true;
    }

    if (t.kind == NIL)
    {
        take();
        return true;
    }

    if (accept("-"))
    {
        if (!factor(out))
            return false;

        /* A number negates; a signal becomes a mul by -1, which is the node
           the desugar already has. A unit rides along -- `-5 ms' -- and is
           never on a signal, which unitFactor refuses. */
        if (out.expr == NULL)
            out.num = -out.num;
        else
            out.expr = thExprOp('*', out.expr, thExprConst(-1));

        return true;
    }

    if (accept("@"))
    {
        std::string name;

        if (!word(name, "a control name after '@'"))
            return false;

        out.expr = thExprChanRef(name);
        return true;
    }

    if (t.kind != WORD)
        return expected("a value");

    take();

    /* `osc->out': a bare one becomes an ARG_POINTER and no node, in
       assignment(); under an operator, a leaf of the tree. */
    if (accept("->"))
    {
        std::string arg;

        if (!word(arg, "an arg name after '->'"))
            return false;

        out.expr = thExprNodeRef(t.src->text, arg);
        return true;
    }

    if (!isPunct(peek(), "("))
    {
        error(t, "'" + t.src->text + "' is not a value here; reading a "
              "node is spelled '" + t.src->text + "->out'");
        return false;
    }

    take();

    std::vector<Value> argv;

    for (;;)
    {
        Value a;

        if (!expression(a))
            break;

        argv.push_back(a);

        if (argv.size() == 3 || !accept(","))
        {
            if (expect(")"))
                return call(t, out, t.src->text, argv);

            break;
        }
    }

    for (size_t i = 0; i < argv.size(); i++)
        thExprFree(argv[i].expr);

    return false;
}

/* False, having complained, if either operand was written with a unit. */
bool
Parser::noUnits (const Tok &at, const char *a, const char *b)
{
    const char *unit = a ? a : b;

    if (unit == NULL)
        return true;

    error(at, std::string("'") + unit + "' cannot be used in arithmetic; "
          "write the number the engine wants, or the unit on its own");

    return false;
}

/* False, having said so and freed it, for an expression too deep to free or
   copy by recursion -- see TH_EXPR_DEPTH_MAX. */
bool
Parser::deepEnough (const Tok &at, Value &out)
{
    if (out.expr == NULL)
        return false;

    if (out.expr->depth <= TH_EXPR_DEPTH_MAX)
        return true;

    error(at, "this expression is nested too deeply");
    thExprFree(out.expr);
    out.expr = NULL;

    return false;
}

static thExprNode *
operand (const Value &v)
{
    return v.expr ? v.expr : thExprConst(v.num);
}

/* One arithmetic step. Folds two numbers, builds a tree the moment either
   side is a signal, and consumes both operands whether it succeeds or not.
   `a' may be `out'. */
bool
Parser::arith (const Tok &at, Value &out, int op, Value a, Value b)
{
    out = Value();

    /* Scaling a number that has a unit by one that has none: `th_sample / 2'
     * is half a second, `2 * 5 ms' is 10 ms. Both folds are a
     * multiplication, so scaling the literal and folding it is exactly
     * folding it and scaling the result, and the unit can ride along.
     * Nothing else keeps one: a sum has none this grammar can name, a unit
     * in a denominator is not a unit it has, and a signal is not folded.
     * Nor does a gain in dB or a ratio in cents, whose folds are not a
     * multiplication: `2 * 3 dB' would quietly be 6 dB. */
    if (a.expr == NULL && b.expr == NULL &&
        !thUnitIsLogarithmic(a.units) && !thUnitIsLogarithmic(b.units) &&
        ((op == '*' && (a.units == NULL) != (b.units == NULL)) ||
         (op == '/' && a.units != NULL && b.units == NULL)))
    {
        out.num = (op == '*') ? a.num * b.num : a.num / b.num;
        out.units = a.units ? a.units : b.units;

        return true;
    }

    if (!noUnits(at, a.units, b.units))
    {
        thExprFree(a.expr);
        thExprFree(b.expr);

        return false;
    }

    /* Two numbers fold, including `1 / 0', which this language has always
       let through as an infinity and which the non-finite guard reports at
       the voice that reaches it. */
    if (a.expr == NULL && b.expr == NULL)
    {
        /* An integer division by zero is a SIGFPE and not a number. */
        if (op == '%' && (int)b.num == 0)
        {
            error(at, "'%' by zero has no value");
            return false;
        }

        switch (op)
        {
        case '+': out.num = a.num + b.num; break;
        case '-': out.num = a.num - b.num; break;
        case '*': out.num = a.num * b.num; break;
        case '/': out.num = a.num / b.num; break;
        case '%': out.num = (float)(((int)a.num) % ((int)b.num)); break;
        }

        return true;
    }

    /* Every other operator has a math:: node behind it and this one does
       not. Saying so beats desugaring to something that is nearly a
       modulo. */
    if (op == '%')
    {
        error(at, "'%' takes two numbers; there is no node for a modulo "
                  "over signals");
        thExprFree(a.expr);
        thExprFree(b.expr);

        return false;
    }

    out.expr = thExprOp(op, operand(a), operand(b));

    return deepEnough(at, out);
}

/* `pow(a, b)'. Consumes `argv' whether it succeeds or not. */
bool
Parser::call (const Tok &at, Value &out, const std::string &name,
              std::vector<Value> &argv)
{
    std::vector<thExprNode *> kids;
    std::string why;
    bool ok = true;

    out = Value();

    for (size_t i = 0; i < argv.size(); i++)
    {
        if (argv[i].units && !noUnits(at, argv[i].units, NULL))
            ok = false;

        kids.push_back(operand(argv[i]));
    }

    if (ok)
        out.expr = thExprCall(name, kids, why);

    /* thExprCall empties `kids' whether it succeeds or not; this is the
       units path, which never reached it. */
    for (size_t i = 0; i < kids.size(); i++)
        thExprFree(kids[i]);

    if (ok && out.expr == NULL)
        error(at, why);

    if (!deepEnough(at, out))
        return false;

    /* `exp2(2)' is 4 and no node, for the same reason `5 * 2' is 10 and no
       node: a function over constants is a constant. */
    if (out.expr->kind == thExprNode::CONST)
    {
        out.num = out.expr->value;
        thExprFree(out.expr);
        out.expr = NULL;
    }

    return true;
}

/* Everything after the lex, which is the only step a stream and a string
 * do differently. `lexed' is the lexer's answer; `tokens' its output either
 * way. */
int
parseLexed (thSynth *synth, std::vector<thLexToken> &tokens, bool lexed,
            thSynthTree **treeOut)
{
    *treeOut = new thSynthTree("newmod", synth);

    /* A lexical error stops the file here: a character the language cannot
       spell is a broken file, and a broken file should not half-build a
       tree. */
    if (!lexed)
    {
        const thLexToken *bad = thLexError(tokens);

        fprintf(stderr, "line %d: error: %s\n",
                bad ? bad->line : 0,
                bad ? bad->text.c_str() : "lexical error");

        return 1;
    }

    std::string why;
    int line = 0;

    if (!thExpandArrays(tokens, why, line))
    {
        fprintf(stderr, "line %d: error: %s\n", line, why.c_str());
        return 1;
    }

    Parser parser(synth, *treeOut, tokens);

    return parser.statements() ? 0 : 1;
}

} /* namespace */

int thParseDsp (thSynth *synth, FILE *input, thSynthTree **treeOut)
{
    /* Checked rather than assumed: this is the entry point an out-of-tree
       consumer of libthink reaches the language through. A nonzero return
       with *treeOut left NULL is what a parse that failed on line one looks
       like to finishParse already. */
    if (treeOut == NULL)
        return 1;

    *treeOut = NULL;

    if (synth == NULL || input == NULL)
        return 1;

    std::vector<thLexToken> tokens;
    const bool lexed = thLexStream(input, tokens);

    return parseLexed(synth, tokens, lexed, treeOut);
}

int thParseDspText (thSynth *synth, const std::string &text,
                    thSynthTree **treeOut)
{
    if (treeOut == NULL)
        return 1;

    *treeOut = NULL;

    if (synth == NULL)
        return 1;

    std::vector<thLexToken> tokens;
    const bool lexed = thLexString(text, tokens);

    return parseLexed(synth, tokens, lexed, treeOut);
}
