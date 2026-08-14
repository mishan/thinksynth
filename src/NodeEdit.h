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

#ifndef NODE_EDIT_H
#define NODE_EDIT_H 1

/*
 * Changing one value in a .dsp, by editing the text.
 *
 * The alternative -- re-emitting the parsed model -- would lose the 391
 * comments across the corpus, fold `5 ms' to `220.5', and write out dozens of
 * args buildArgMap() synthesised that nobody authored. So this finds the one
 * assignment being changed and rewrites only its right-hand side. Every other
 * byte of the file is copied through.
 *
 * What the grammar allows is narrower than it looks, and the writer is held to
 * it:
 *
 *   - Numbers are `[0-9]+(\.[0-9]*)?'. No exponent, so a value that needs one
 *     cannot be written at all.
 *   - Negatives exist only as a unary-minus rule over that, so `-0.5' is fine
 *     but `-5 ms' would parse as -(5 ms) -- which is what we want anyway.
 *   - `5 ms' means 5 * TH_SAMPLE / 1000 and `50%' means 50 * TH_MAX / 100.
 *     Both are exactly invertible, so a value written with a unit comes back
 *     with the same unit rather than as a folded raw number.
 *
 * Anything it cannot express, it refuses and says why, rather than writing
 * something that parses to a different number.
 */

#include <string>

using std::string;

class NodeEdit {
public:
    enum Result {
        OK = 0,
        NO_NODE,        /* no `node <name>' block in the file      */
        NOT_A_VALUE,    /* the arg is wired to something           */
        UNWRITABLE,     /* no way to spell this number             */
        REFUSED,        /* the edit does not make sense            */
        IO_ERROR
    };

    /* Sets `<node> { <arg> = <value>; }'.
     *
     * If the block has no such assignment -- the arg exists only because
     * buildArgMap() invented it -- one is inserted before the closing brace.
     *
     * `why' gets a sentence suitable for showing to a person.
     */
    static Result setValue (const string &filename, const string &node,
                            const string &arg, double value, string &why);

    /* Points `<node>.<arg>' at `<srcNode>-><srcPort>'. Adds the line if the
       file did not have one. Connecting something to where it already is
       connected changes no byte of the file.

       Does not judge whether the connection is sensible -- that needs the
       graph, which knows the io node is really two boxes. See
       NodeGraph::canConnect. */
    static Result connect (const string &filename, const string &node,
                           const string &arg, const string &srcNode,
                           const string &srcPort, string &why);

    /* Points `<node>.<arg>' at a control: `@<control>'.
    
       Separate from connect() rather than inferred from the source name,
       because the spelling is genuinely different -- `@blim', not
       `blim->blim' -- and guessing from a name that happens to start with an
       @ would be the kind of cleverness that fails on the one .dsp that names
       a node oddly. The caller knows which it has; the graph marks it. */
    static Result connectControl (const string &filename, const string &node,
                                  const string &arg, const string &control,
                                  string &why);

    /* Replaces a connection -- a `node->port' or a `@control' -- with a plain
       value, leaving the line in place. To the engine `in = 0;' and no line at
       all are the same thing, and keeping the line preserves its trailing
       comment and makes a reconnect restore the file exactly. */
    static Result disconnect (const string &filename, const string &node,
                              const string &arg, double value, string &why);

    /* Sets a top-level `@<name> = <value>;' -- one of the .dsp's control
       declarations.

       These live outside any node block, so this looks for the line at brace
       depth zero rather than inside a `node' body. As with setValue, writing
       the value already there changes no byte. */
    static Result setChanArg (const string &filename, const string &name,
                              double value, string &why);

    /* Appends `node <name> <cat>::<plugin> { };' before the `io' line.
    
       Before the io line because the grammar wants every node defined by the
       time `io <name>;' names one, and because that is where every shipped
       .dsp puts its last node. Refuses a name the file already uses, and a
       name the lexer would not accept as a WORD. */
    static Result addNode (const string &filename, const string &node,
                           const string &plugin, string &why);

    /* Removes a node's block, and every `= <node>->...' that referred to it.
    
       Both, because leaving the references would make the file stop parsing:
       setPointers warns and the node resolves to nothing. `removed' gets the
       count of references rewritten, which is worth telling the user -- it is
       the part of a delete they did not ask for. */
    static Result removeNode (const string &filename, const string &node,
                              int &removed, string &why);

    /* Writes a new .dsp containing nothing but what it takes to load: the
       three info strings, an io node, and the `io' line naming it.
    
       There is no such thing as a valid empty .dsp -- without an io node
       finishParse rejects the file -- so "new" has to mean this rather than a
       blank page. Refuses to overwrite. */
    static Result createFile (const string &filename, const string &name,
                              const string &author, string &why)
    {
        return createFile(filename, name, author, false, why);
    }

    /* With `replace', writes over a file that is already there.

       For a caller that has already asked -- the GUI's Save dialog does its
       own overwrite confirmation. The point of the flag is that such a caller
       no longer has to delete the file first to get past the refusal above:
       doing that destroyed the user's .dsp before finding out whether the new
       one could be written at all. writeLines renames a temporary into place,
       so the old file survives until the new one is complete. */
    static Result createFile (const string &filename, const string &name,
                              const string &author, bool replace, string &why);

    /* True if `name' is something the lexer will read back as a node name. */
    static bool validName (const string &name);

    /* Adds a control: the five-line `@name' block a .dsp uses to declare
       something worth playing with.
    
           @blim = 0.5;
           @blim.widget = 1;
           @blim.min = 0;
           @blim.max = 2;
           @blim.label = "Band Limit";
    
       Written before the first node, which is where all 206 of them sit in
       the shipped files. The label may be empty; it may not contain a quote,
       because the lexer's string is `"[^"\n]*"' with no escapes at all. */
    static Result addControl (const string &filename, const string &name,
                              double value, double min, double max,
                              const string &label, string &why)
    {
        return addControl(filename, name, value, min, max, label, "", why);
    }

    /* With a group: `@name.group = "Envelope"'. Controls sharing a group are
       drawn as one titled block against the node they drive, which is how an
       ADSR's four sliders are actually thought about. Same constraint as the
       label -- no quote, the lexer has no escape for one. */
    static Result addControl (const string &filename, const string &name,
                              double value, double min, double max,
                              const string &label, const string &group,
                              string &why);

    /* Changes a control's range, label and group -- the `@name.min', `.max',
       `.label' and `.group' lines around a declaration that already exists.

       The same splice as setChanArg, applied four times and written once, so a
       range change is one rename-into-place rather than four. Everything the
       single-value case obeys applies here too. A `.max' spelled `2000ms'
       keeps its unit, because the grammar's conversion is exactly invertible
       and handing back the folded sample count would be correct and
       unreadable. A range is compared by value rather than by spelling, so a
       `.max' of `th_max' survives a write of 1 with its six characters intact.
       And a range written with arithmetic, or with a name this cannot read, is
       UNWRITABLE rather than quietly replaced by the number it happens to
       evaluate to today -- both halves of the range, since half a range this
       editor cannot read back is worse than none of it.

       A line the file does not have is added at the end of the control's
       block. The parser only requires that `@x.min' follow `@x' -- before it
       there is nothing to modify -- so the canonical order addControl() writes
       is for a reader rather than a requirement, and reordering someone's file
       to match it would be an edit they did not ask for.

       An empty label or group *removes* that line rather than writing `""'. A
       control that never had a label and one whose label was cleared should be
       the same file.

       If the new range no longer contains the control's value, the value is
       clamped into it. Narrowing a range around a value outside it otherwise
       leaves a slider that cannot reach what the file says and that moves the
       instant it is touched. */
    static Result setControlMeta (const string &filename, const string &name,
                                  double min, double max, const string &label,
                                  const string &group, string &why);

    /* Removes a control's whole block, and rewrites every `= @name' that
       read from it. Same reasoning as removeNode: a dangling reference makes
       the file load with the arg silently reading zero. */
    static Result removeControl (const string &filename, const string &name,
                                 int &removed, string &why);

    /* True if `label' can be written as a .dsp string. */
    static bool validLabel (const string &label);

    /* OK if the file has an explicit `<node> { <arg> = ...; }'. Distinguishes
       an arg the author wrote from one buildArgMap() invented, which is the
       difference between changing a line and adding one. */
    static Result find (const string &filename, const string &node,
                        const string &arg);

    /* Formats a number the way the grammar accepts it: plain decimal, no
       exponent, no trailing zeros. Returns false if it cannot be done, which
       is what the caller must not paper over. */
    static bool format (double value, string &out);

    /* Formats `value' using the unit `units' ("ms", "%", or empty), inverting
       the grammar's conversion so the file keeps the unit it had. */
    static bool formatWithUnits (double value, const string &units,
                                 string &out);

    /* The unit suffix on a right-hand side, or "" if there is none. Public
       because the round-trip check wants to assert on it. */
    static string unitsOf (const string &rhs);

    static const char *resultText (Result r);
};

#endif /* NODE_EDIT_H */
