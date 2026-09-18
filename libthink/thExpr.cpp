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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>     /* strtof, for the shortest round-tripping decimal */
#include <math.h>

#include "thExpr.h"

using std::string;
using std::vector;

thExprNode::~thExprNode (void)
{
    for (size_t i = 0; i < kids.size(); i++)
        delete kids[i];
}

/* What each function computes when its operands are all numbers: its
   plugin's callback with the buffers taken out. Written here rather than
   called through because a plugin is a .so this library does not link, and
   character for character with the callback because a folded `exp2(2)' and
   a math::exp2 node fed a 2 have to be the same float. */
static float foldPow   (float a, float b, float)   { return powf(a, b); }
static float foldExp2  (float a, float, float)     { return exp2f(a); }
static float foldAbs   (float a, float, float)     { return fabsf(a); }

/* Comparisons rather than fminf/fmaxf, which return the non-NaN operand and
   would swallow exactly what the voice guard exists to catch. math::min and
   math::max say the same thing in the same words. */
static float foldMin   (float a, float b, float)   { return a < b ? a : b; }
static float foldMax   (float a, float b, float)   { return a > b ? a : b; }

/* math::clamp's order, and its consequence: an inverted range is not sorted
   for the author, so below `lo' the answer is `lo' and at or above it `hi'. */
static float foldClamp (float a, float b, float c)
{
    return a < b ? b : (a > c ? c : a);
}

/* The function set.
 *
 * `exp2' and `pow' are here because a detune in cents is
 * `exp2(@cents / 1200)' and there is no operator that spells it; the other
 * four because a graph that wants a magnitude or a bound should not have to
 * be a tree of fades to get one. Each is a plugin as well, so what an
 * expression desugars to is something a .dsp could also have written by
 * hand. */
static const thExprFunc functions[] = {
    { "pow",   "math/pow",   2, { "base", "exp",  NULL }, foldPow   },
    { "exp2",  "math/exp2",  1, { "in",   NULL,   NULL }, foldExp2  },
    { "abs",   "math/abs",   1, { "in",   NULL,   NULL }, foldAbs   },
    { "min",   "math/min",   2, { "in0",  "in1",  NULL }, foldMin   },
    { "max",   "math/max",   2, { "in0",  "in1",  NULL }, foldMax   },
    { "clamp", "math/clamp", 3, { "in",   "lo",   "hi"  }, foldClamp },
};

/* The node an operator becomes. `%' is absent on purpose: there is no modulo
   over signals, and both loaders say so rather than desugaring to something
   that is nearly one. */
static const char *
opPlugin (int op)
{
    switch (op)
    {
    case '+': return "math/add";
    case '-': return "math/sub";
    case '*': return "math/mul";
    case '/': return "math/div";
    }

    return NULL;
}

bool
thExprPlugin (const thExprNode *e, const char *&path, int &arity,
              const char *argname[3])
{
    path = NULL;
    arity = 0;
    argname[0] = argname[1] = argname[2] = NULL;

    if (e == NULL)
        return false;

    if (e->kind == thExprNode::OP)
    {
        path = opPlugin(e->op);
        arity = 2;
        argname[0] = "in0";
        argname[1] = "in1";

        return path != NULL;
    }

    if (e->kind != thExprNode::CALL)
        return false;

    const thExprFunc *f = thExprLookup(e->name);

    if (f == NULL)
        return false;

    path = f->plugin;
    arity = f->arity;

    for (int i = 0; i < arity; i++)
        argname[i] = f->args[i];

    return true;
}

const thExprFunc *
thExprLookup (const string &name)
{
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++)
        if (name == functions[i].name)
            return &functions[i];

    return NULL;
}

thExprNode *
thExprConst (float value)
{
    thExprNode *e = new thExprNode(thExprNode::CONST);

    e->value = value;

    return e;
}

thExprNode *
thExprNodeRef (const string &node, const string &arg)
{
    thExprNode *e = new thExprNode(thExprNode::NODEREF);

    e->node = node;
    e->arg = arg;

    return e;
}

thExprNode *
thExprChanRef (const string &name)
{
    thExprNode *e = new thExprNode(thExprNode::CHANREF);

    e->name = name;

    return e;
}

static bool
isConst (const thExprNode *e)
{
    return e != NULL && e->kind == thExprNode::CONST;
}

thExprNode *
thExprOp (int op, thExprNode *a, thExprNode *b)
{
    if (a == NULL || b == NULL)
    {
        delete a;
        delete b;

        return NULL;
    }

    /* Folded here rather than in the grammar so that `(2 + 3) * x' is
       `5 * x' -- the grammar can only fold what it reduces, and a constant
       subtree under a signal one is still a constant. Division by a zero
       constant is left standing: folding it would put an infinity in an arg
       at parse time, where nothing reports it, rather than at the voice,
       where the non-finite guard names the channel and the graph. */
    if (isConst(a) && isConst(b) && !(op == '/' && b->value == 0))
    {
        float v = 0;

        switch (op)
        {
        case '+': v = a->value + b->value; break;
        case '-': v = a->value - b->value; break;
        case '*': v = a->value * b->value; break;
        case '/': v = a->value / b->value; break;
        default:
            delete a;
            delete b;
            return NULL;
        }

        delete a;
        delete b;

        return thExprConst(v);
    }

    thExprNode *e = new thExprNode(thExprNode::OP);

    e->op = op;
    e->kids.push_back(a);
    e->kids.push_back(b);

    return e;
}

static float
callConst (const thExprFunc *f, const vector<thExprNode *> &kids)
{
    const float a = kids[0]->value;
    const float b = f->arity > 1 ? kids[1]->value : 0;
    const float c = f->arity > 2 ? kids[2]->value : 0;

    return f->fold(a, b, c);
}

thExprNode *
thExprCall (const string &name, vector<thExprNode *> &kids, string &why)
{
    const thExprFunc *f = thExprLookup(name);

    if (f == NULL)
    {
        why = "'" + name + "' is not a function";
    }
    else if ((int)kids.size() != f->arity)
    {
        char buf[128];

        snprintf(buf, sizeof(buf), "%s() takes %d argument%s, not %d",
                 f->name, f->arity, f->arity == 1 ? "" : "s",
                 (int)kids.size());
        why = buf;
    }
    else
    {
        bool allConst = true;

        for (size_t i = 0; i < kids.size(); i++)
            if (!isConst(kids[i]))
                allConst = false;

        if (allConst)
        {
            const float v = callConst(f, kids);

            for (size_t i = 0; i < kids.size(); i++)
                delete kids[i];

            kids.clear();

            return thExprConst(v);
        }

        thExprNode *e = new thExprNode(thExprNode::CALL);

        e->name = f->name;
        e->kids = kids;

        kids.clear();

        return e;
    }

    for (size_t i = 0; i < kids.size(); i++)
        delete kids[i];

    kids.clear();

    return NULL;
}

void
thExprFree (thExprNode *e)
{
    delete e;
}

bool
thExprHasSignal (const thExprNode *e)
{
    if (e == NULL)
        return false;

    if (e->kind == thExprNode::NODEREF || e->kind == thExprNode::CHANREF)
        return true;

    for (size_t i = 0; i < e->kids.size(); i++)
        if (thExprHasSignal(e->kids[i]))
            return true;

    return false;
}

static void
collectLeaves (const thExprNode *e, vector<thExprLeaf> &out)
{
    if (e == NULL)
        return;

    if (e->kind == thExprNode::NODEREF || e->kind == thExprNode::CHANREF)
    {
        thExprLeaf leaf;

        leaf.isChan = (e->kind == thExprNode::CHANREF);
        leaf.node = leaf.isChan ? e->name : e->node;
        leaf.arg = leaf.isChan ? "" : e->arg;

        for (size_t i = 0; i < out.size(); i++)
            if (out[i].isChan == leaf.isChan && out[i].node == leaf.node &&
                out[i].arg == leaf.arg)
                return;

        out.push_back(leaf);

        return;
    }

    for (size_t i = 0; i < e->kids.size(); i++)
        collectLeaves(e->kids[i], out);
}

void
thExprLeaves (const thExprNode *e, vector<thExprLeaf> &out)
{
    out.clear();

    collectLeaves(e, out);
}

/* Plain decimal, and the shortest one that reads back as this same float --
   the shape the grammar accepts, so the text this produces is text a .dsp
   could hold.
 *
 * `%g' was neither. It switches to an exponent outside 1e-4 .. 1e+6, and the
 * lexer's number is `[0-9]+(\.([0-9]+)?)?' with no exponent at all, so a
 * small constant came out `1e-07'; and it rounds to six significant digits,
 * so `1.2345678' came out `1.23457'. Both put a number in the editor's box
 * that the author did not write and the language cannot take back.
 *
 * Widening until strtof agrees rather than picking a fixed precision: 9
 * digits round-trips every float but writes `0.100000001' for a tenth, and
 * the first precision that round-trips is both exact and what a reader
 * expects. A non-finite value matches at once and comes out `inf' or `nan',
 * which no .dsp can hold either -- but saying so is better than rounding it
 * to something that looks finite. */
static string
number (float v)
{
    char buf[512];

    for (int prec = 0; prec < 45; prec++)
    {
        snprintf(buf, sizeof(buf), "%.*f", prec, (double)v);

        if (strtof(buf, NULL) == v)
            break;
    }

    return string(buf);
}

static int
precedence (const thExprNode *e)
{
    if (e->kind != thExprNode::OP)
        return 3;       /* a leaf or a call binds tighter than any operator */

    return (e->op == '+' || e->op == '-') ? 1 : 2;
}

static string
text (const thExprNode *e)
{
    if (e == NULL)
        return "";

    switch (e->kind)
    {
    case thExprNode::CONST:
        return number(e->value);

    case thExprNode::NODEREF:
        return e->node + "->" + e->arg;

    case thExprNode::CHANREF:
        return "@" + e->name;

    case thExprNode::CALL:
    {
        string s = e->name + "(";

        for (size_t i = 0; i < e->kids.size(); i++)
            s += (i ? ", " : "") + text(e->kids[i]);

        return s + ")";
    }

    case thExprNode::OP:
        break;
    }

    /* Parenthesised where precedence needs it, and on the right of `-' and
       `/' where associativity does: `a - (b - c)' is not `a - b - c'.
     *
     * `<=' and not `<' on the left, because every operator in this grammar
     * groups to the right. A left operand of equal precedence is exactly the
     * one that needs the parentheses: `(a - 1) - 2' printed as `a - 1 - 2'
     * reads back as `a - (1 - 2)', which is `a + 1' -- a box showing the
     * author arithmetic their patch is not doing. A leaf or a call scores 3
     * and so is never wrapped by this. */
    const int mine = precedence(e);

    string left = text(e->kids[0]);
    string right = text(e->kids[1]);

    if (precedence(e->kids[0]) <= mine)
        left = "(" + left + ")";

    if (precedence(e->kids[1]) < mine ||
        (precedence(e->kids[1]) == mine && (e->op == '-' || e->op == '/')))
        right = "(" + right + ")";

    return left + " " + (char)e->op + " " + right;
}

string
thExprText (const thExprNode *e)
{
    return text(e);
}
