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

#ifndef THCGENEDIT_H
#define THCGENEDIT_H

#include <string>
#include <utility>
#include <vector>

/*
 * Changing a .gen file, by editing the text.
 *
 * The same decision NodeEdit made for .dsp, for the same reason: a piece
 * file is the author's document, and re-emitting a parsed model would
 * lose every comment and every deliberate blank line in it. So each
 * operation here finds the exact byte span it is aimed at -- through the
 * loader's own tokenizer, which is what makes "the editor reads the
 * language the loader reads" true by construction -- and replaces that
 * span. Everything between the spans is copied through untouched.
 *
 * Two properties fall out of splicing token spans rather than lines:
 * multiple statements on one line (`period = 23.9 s; jitter = 2.0 s;`,
 * which the shipped piece is full of) are edited independently, and a
 * value's authored unit is preserved because the caller writes the whole
 * right-hand side and never sees a folded number.
 *
 * GEN_FORMAT.md §7's writer's rules bind what gets *written into* the
 * spans: durations always carry a unit, knob bindings are spelled @name,
 * a stage added by the GUI writes every param the plugin registers, and
 * seed appears if and only if the user pinned one.
 *
 * Everything operates on a file, NodeEdit-style, because the editor
 * keeps its edits in a work copy and publishes on Save -- the same flow
 * NodeEditor uses, so a crashed session never half-writes the original.
 */
class thcGenEdit {
public:
    enum Result {
        OK = 0,
        NOT_FOUND,      /* no such chain/stage/knob/... in the file */
        REFUSED,        /* the edit does not make sense             */
        UNWRITABLE,     /* no way to spell this value               */
        IO_ERROR
    };

    static const char *resultText (Result r);

    /* ---- what the file says, for the editor to draw ------------------
     *
     * A structural description with the *authored* right-hand sides:
     * "19.4 s", "@density", "fmin", "\"F3 Ab3 C4\"" -- exactly the text
     * a splice would replace, so what the panel shows is what the file
     * says rather than what the engine folded it to. */

    struct Param { std::string name; std::string valueText; };

    struct Stage
    {
        std::string name, category, plugin;
        std::vector<Param> params;
    };

    struct Sink { int channel; std::string chanarg; };

    struct Chain
    {
        std::string name;
        bool inputMidi;
        std::vector<Stage> stages;
        std::vector<Sink> sinks;
    };

    struct Knob
    {
        std::string name;
        double value;
        bool   hasMin, hasMax;
        double min, max;
        std::string label;
    };

    struct Scale { std::string name, notes; };

    struct Doc
    {
        std::string name, author, description;
        bool     hasSeed;
        unsigned seed;
        bool     hasTempo;
        double   tempo;
        std::vector<Knob>  knobs;
        std::vector<Scale> scales;
        std::vector<Chain> chains;
    };

    static Result describe (const std::string &filename, Doc &doc,
                            std::string &why);

    /* ---- the piece ---------------------------------------------------- */

    /* key is "name", "author" or "description". Empty text removes the
       line; a piece that never had an author and one whose author was
       cleared should be the same file. */
    static Result setInfo (const std::string &filename, const std::string &key,
                           const std::string &text, std::string &why);

    /* Pinning and unpinning. Written near the top, where the writer's
       rules put it; clearSeed exists because a generated file keeping a
       seed the user un-pinned would silently freeze a piece that was
       meant to breathe. */
    static Result setSeed (const std::string &filename, unsigned seed,
                           std::string &why);
    static Result clearSeed (const std::string &filename, std::string &why);

    static Result setTempo (const std::string &filename, double bpm,
                            std::string &why);
    static Result clearTempo (const std::string &filename, std::string &why);

    /* ---- knobs -------------------------------------------------------- */

    static Result setKnobValue (const std::string &filename,
                                const std::string &name, double value,
                                std::string &why);

    /* Range and label together, one write -- the same shape as
       NodeEdit::setControlMeta and for the same reason. An empty label
       removes the .label line. */
    static Result setKnobMeta (const std::string &filename,
                               const std::string &name, double min,
                               double max, const std::string &label,
                               std::string &why);

    /* The standard block: @name = v; .widget/.min/.max[/.label]. */
    static Result addKnob (const std::string &filename,
                           const std::string &name, double value,
                           double min, double max, const std::string &label,
                           std::string &why);

    /* Removes the block and rewrites every `= @name' binding to the
       plain value `fallback' -- a dangling knob reference is a load
       error, and the knob's last value is what those params were
       actually hearing. `rewritten' counts the bindings replaced,
       which is the part of the delete the user did not ask for. */
    static Result removeKnob (const std::string &filename,
                              const std::string &name, double fallback,
                              int &rewritten, std::string &why);

    /* ---- scales ------------------------------------------------------- */

    static Result addScale (const std::string &filename,
                            const std::string &name,
                            const std::string &notes, std::string &why);
    static Result setScale (const std::string &filename,
                            const std::string &name,
                            const std::string &notes, std::string &why);

    /* References are inlined as the scale's literal note list, so the
       music does not change out from under the chains that used it. */
    static Result removeScale (const std::string &filename,
                               const std::string &name, int &rewritten,
                               std::string &why);

    /* ---- chains ------------------------------------------------------- */

    /* A new chain arrives whole -- one generator stage with `params'
       (every registered param, per the writer's rules; the caller
       builds them from the plugin's defaults) and one note sink --
       because a chain with no generator and no input does not load,
       and every intermediate state this editor writes must load. */
    static Result addChain (const std::string &filename,
                            const std::string &name, int channel,
                            const std::string &stageName,
                            const std::string &category,
                            const std::string &plugin,
                            const std::vector<std::pair<std::string,
                                std::string> > &params,
                            std::string &why);

    static Result removeChain (const std::string &filename,
                               const std::string &name, std::string &why);
    static Result renameChain (const std::string &filename,
                               const std::string &oldName,
                               const std::string &newName, std::string &why);

    static Result setChainInput (const std::string &filename,
                                 const std::string &chain, bool midi,
                                 std::string &why);

    /* ---- stages ------------------------------------------------------- */

    /* Inserted before the first sink: textual order is execution order,
       and sinks come last. Same all-params rule as addChain. */
    static Result addStage (const std::string &filename,
                            const std::string &chain,
                            const std::string &stageName,
                            const std::string &category,
                            const std::string &plugin,
                            const std::vector<std::pair<std::string,
                                std::string> > &params,
                            std::string &why);

    static Result removeStage (const std::string &filename,
                               const std::string &chain, int stageIndex,
                               std::string &why);

    /* Reorder: the whole stage block moves; its text moves with it,
       comments included. */
    static Result moveStage (const std::string &filename,
                             const std::string &chain, int from, int to,
                             std::string &why);

    /* Replaces the right-hand side of `<param> = ...;` in one stage, or
       inserts the line if the stage body does not have it (the param
       existed only as the plugin's default). `valueText' is the whole
       RHS as it should appear: "0.5", "19.4 s", "@density", "fmin",
       "\"F3 Ab3\"" -- validated against the lexer before anything is
       written. */
    static Result setParam (const std::string &filename,
                            const std::string &chain, int stageIndex,
                            const std::string &param,
                            const std::string &valueText, std::string &why);

    /* ---- sinks -------------------------------------------------------- */

    static Result addSink (const std::string &filename,
                           const std::string &chain, int channel,
                           const std::string &chanarg, std::string &why);

    /* Refuses to remove the last one: a chain with no sink is a load
       error, and every state this editor writes must load. */
    static Result removeSink (const std::string &filename,
                              const std::string &chain, int sinkIndex,
                              std::string &why);

    /* Channel and chanarg together. An empty chanarg makes it a note
       sink (the `chanarg = "...";' is removed, not written empty). */
    static Result setSink (const std::string &filename,
                           const std::string &chain, int sinkIndex,
                           int channel, const std::string &chanarg,
                           std::string &why);

    /* ---- small shared checks ------------------------------------------ */

    /* True if the lexer will read `name' back as one WORD. */
    static bool validName (const std::string &name);

    /* Plain decimal, no exponent, no trailing zeros -- the only spelling
       the lexer reads. False when the value cannot be written, which the
       caller must not paper over. */
    static bool format (double value, std::string &out);
};

#endif /* THCGENEDIT_H */
