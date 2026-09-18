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

#ifndef TH_EXPR_H
#define TH_EXPR_H 1

/*
 * `osc2.freq = freq->out * 0.5' -- arithmetic over signals, between the
 * parse and the graph.
 *
 * The language already says a constant is a buffer of constants and that
 * modulation is not a special case, so `freq->out * 0.5' needs nothing new
 * defined: it is per-sample arithmetic, and scalar and vector are the same
 * expression. The audio path never sees one of these. Each becomes the
 * math:: nodes it stands for at load, in thSynthTree::desugarExprs, and
 * everything downstream -- probes, layout, dspcheck, the wasm build,
 * thcNodeHost -- sees an ordinary graph.
 *
 * An all-constant expression never reaches here. The grammar folds those at
 * parse as it always has, so every shipped file produces the same number and
 * the same node count it did before this existed.
 */

#include <string>
#include <vector>

#include "thExport.h"

struct thExprNode
{
    enum Kind
    {
        CONST,      /* a number                                          */
        NODEREF,    /* `node->arg'                                       */
        CHANREF,    /* `@name'                                           */
        OP,         /* kids[0] <op> kids[1]                              */
        CALL        /* fn(kids...)                                       */
    };

    Kind   kind;
    float  value;               /* CONST                                 */
    std::string node, arg;      /* NODEREF                               */
    std::string name;           /* CHANREF's name, CALL's function       */
    int    op;                  /* OP: '+' '-' '*' '/'                   */

    std::vector<thExprNode *> kids;

    thExprNode (Kind k) : kind(k), value(0), op(0) { }
    ~thExprNode (void);
};

/* One function the language offers, and the plugin it becomes.
 *
 * Functions rather than operators -- `pow(a, b)' and not `a ^ b' -- so
 * there is no new precedence to explain, and so each one is a plugin
 * usable on its own rather than a spelling only the grammar knows. */
struct thExprFunc
{
    const char *name;           /* "pow"                                 */
    const char *plugin;         /* "math/pow"                            */
    int         arity;
    const char *args[3];        /* the plugin's arg names, in order      */

    /* What the constant fold computes, when every operand is a number.
     *
     * A pointer rather than a switch beside the table, because the two would
     * have to be edited together and nothing would say so. Unused operands
     * are passed 0. Each one is the plugin's own arithmetic, in the plugin's
     * own precision -- powf and not pow -- so a folded `exp2(2)' and a
     * `math::exp2' node fed a 2 are the same float and not two floats an ULP
     * apart. */
    float     (*fold)(float a, float b, float c);
};

/* NULL if nothing is called that. */
THINK_API const thExprFunc *thExprLookup (const std::string &name);

/* The plugin an OP or a CALL stands for: its path, its arity, and its arg
 * names in order. False if there is no node for it -- `%' over signals,
 * which both loaders refuse before reaching here.
 *
 * Here rather than in each loader because there are two desugars --
 * thSynthTree's for a .dsp and thcGenLoader's for a .gen -- and an operator
 * that meant `math/add' in one and something else in the other would be the
 * one way the two languages could come apart. */
THINK_API bool thExprPlugin (const thExprNode *e, const char *&path,
                             int &arity, const char *argname[3]);

/* Constructors. Each takes ownership of the nodes handed to it.
 *
 * thExprOp and thExprCall fold when every operand is a CONST, which is what
 * keeps `a = 5 * 2' one number and no nodes. A fold that would divide by
 * zero is left as a tree instead: math::div's own description says a zero
 * denominator is a non-finite result, and the guard in thMidiChan reports
 * one at the voice that produced it.
 *
 * That last rule reaches .gen and not .dsp. thinklang.yy folds two numbers
 * in its own action before it ever builds a node, so `a = 1 / 0' in a .dsp
 * is the infinity it has always been, which is what keeps the corpus
 * rendering as it did; .gen has no such shortcut and gets the node. Both
 * are deliberate, and exprcheck and gencheck pin one each. */
THINK_API thExprNode *thExprConst   (float value);
THINK_API thExprNode *thExprNodeRef (const std::string &node,
                                     const std::string &arg);
THINK_API thExprNode *thExprChanRef (const std::string &name);
THINK_API thExprNode *thExprOp      (int op, thExprNode *a, thExprNode *b);

/* NULL, having freed `kids', if `name' is not a function or the arity is
 * wrong; `why' then says which. */
THINK_API thExprNode *thExprCall    (const std::string &name,
                                     std::vector<thExprNode *> &kids,
                                     std::string &why);

THINK_API void thExprFree (thExprNode *e);

/* True if any leaf is a NODEREF or a CHANREF -- that is, if this has to
 * become nodes rather than a number. */
THINK_API bool thExprHasSignal (const thExprNode *e);

/* The expression as a .dsp would write it, canonically: one space around
 * each operator, parentheses only where precedence needs them. For error
 * messages, and for the read-only box the node editor draws. Not a
 * round-trip of the author's bytes -- nothing rewrites expression text, so
 * there is nothing for this to have to preserve. */
THINK_API std::string thExprText (const thExprNode *e);

/* One signal leaf of an expression, as something a wire can come from. */
struct thExprLeaf
{
    bool        isChan;     /* `@name' rather than `node->arg'          */
    std::string node;       /* the node, or the control's name          */
    std::string arg;        /* the port; empty for a control            */

    thExprLeaf (void) : isChan(false) { }
};

/* Every distinct signal leaf, left to right. Distinct because `a->out * 2 +
 * a->out' reads one output twice and the editor draws one wire, not two --
 * the same thing the graph does for a node arg bound twice. */
THINK_API void thExprLeaves (const thExprNode *e,
                             std::vector<thExprLeaf> &out);

#endif /* TH_EXPR_H */
