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

#ifndef NODE_HISTORY_H
#define NODE_HISTORY_H 1

/*
 * The steps a node editor's Undo and Redo go through.
 *
 * A patch being edited is its text, so a step is a text -- with where its
 * boxes were, which the text does not always say: a box the layout placed
 * and nobody moved has no line in the layout block, and undoing a drag has
 * to put it back all the same.
 *
 * No toolkit and no file: an editor records the state before each edit,
 * and hands its current state over when it undoes or redoes, getting back
 * the state to show.
 */

#include <string>
#include <vector>

#include "NodeLayout.h"

class NodeGraph;

class NodeHistory {
public:
    struct Snapshot {
        std::string text;
        NodeLayout::PosMap positions;
    };

    /* At most `cap' steps back, the oldest forgotten first; edits with one
       join key `joinSeconds' or less apart are one step. */
    explicit NodeHistory (size_t cap = 200, double joinSeconds = 1.0);

    /* An edit called `what' was made, and `was' is the state from before
       it. Edits with the same non-empty `join' -- a panel's slider dragged
       through the values of one arg -- each within joinSeconds of the last,
       by `now' in seconds, are one step back to where the first began, under
       the first one's name. Leaves nothing to redo. */
    void record (const Snapshot &was, const std::string &what,
                 const std::string &join, double now);

    bool canUndo (void) const { return !past_.empty(); }
    bool canRedo (void) const { return !future_.empty(); }

    /* The name of the step Undo or Redo would take, for a tooltip. */
    const std::string &undoName (void) const;
    const std::string &redoName (void) const;

    /* The state to show, given the state shown now -- which goes the other
       way, to be redone or undone again. `out' may be `current'. False with
       nothing to go to. */
    bool undo (const Snapshot &current, Snapshot &out);
    bool redo (const Snapshot &current, Snapshot &out);

    void clear (void);

    /* What a running synth has to be told when `before' becomes `after':
       a control, by its name and with `node' empty, or a node's arg, whose
       value differs.

       Only values a note can take without the graph being rebuilt, and
       `complete' says whether that is all that differs. It is false for a
       node, a control or a wire added or removed, a node's plugin, a text
       arg, an expression's text, and a @name with no slider read as a value
       -- there being no control to tell. Not seen at all: a @name with no slider read only inside
       arithmetic, which the graph holds nowhere, and an arg's elements past
       its first. */
    struct LiveChange {
        bool control;
        std::string node, arg;
        double value;
    };

    static std::vector<LiveChange> liveChanges (const NodeGraph &before,
                                                const NodeGraph &after,
                                                bool &complete);

private:
    struct Step {
        Snapshot state;
        std::string what, join;
        double at;
    };

    void push (std::vector<Step> &to, const Snapshot &state,
               const std::string &what);

    size_t cap_;
    double joinSeconds_;
    std::vector<Step> past_, future_;
};

#endif /* NODE_HISTORY_H */
