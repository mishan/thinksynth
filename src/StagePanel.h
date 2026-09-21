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

#ifndef STAGE_PANEL_H
#define STAGE_PANEL_H 1

/*
 * One composer stage's parameters, as a thPanel.
 *
 * The last of the four providers (see PanelModel.h) and the widest, because
 * a stage's parameter is the one kind whose *spelling* is part of what the
 * author said. A channel's arg is a number in an engine unit and a knob is a
 * number in its own range; `period = 4 beats' is a clocked stage and
 * `period = 2 s' is a free-running one, and the two are different pieces
 * rather than two ways of writing one down.
 *
 * So this is built from the file's own text -- thcGenEdit::describe's
 * authored right-hand sides -- crossed with what the plugin registered.
 * What a row carries is what the line says: the number in the unit it was
 * written in, the unit as one of the three it might have been, the knob it
 * is read through if it is, the note names rather than the MIDI numbers the
 * store holds. The live stage is consulted for one thing only, and it is the
 * one thing the file cannot say: what a knob-bound param is worth at this
 * moment.
 *
 * Which makes the intent a whole authored line, and that is the point of
 * lifting this out. A shell hands over the part the person touched -- a
 * number, or a unit, or a binding -- and this completes it against the row:
 *
 *     "4000"      the number, keeping whatever unit the line had
 *     "ms"        the unit, keeping the number
 *     "@warmth"   read it through that knob from now on
 *     "@"         and stop; hold the number it was showing
 *     "C4 E4 G4"  a note set, as notes; quoted on the way into the file
 *     "warm"      a preset, bare, because that is how a preset is named
 *
 * and answers with the line a .gen would carry. Nothing else knows that
 * rule; both shells draw the controls and neither composes a value.
 *
 * What comes back is a GEN_PARAM intent and there are two halves to
 * delivering one, which is why deliver() does not write the file. The text
 * is the piece, so the lasting half is a splice -- thcGenEdit::setParam, the
 * window's on the desktop and the module's in the browser, both owning the
 * document they splice. The audible half is this one: the running stage's
 * param store, poked so that the edit is heard now rather than at the next
 * load. A shell does both, in that order.
 *
 * Three things are shown and not offered, each for a reason the row's
 * tooltip gives:
 *
 *   arithmetic     `prob = lfo->out * 0.5 + 0.5'. The value in the file is a
 *                  graph, and thcGenEdit::setParam refuses to splice a
 *                  number across one (docs/GEN_FORMAT.md 5a) -- so offering
 *                  a box here would be offering a refusal.
 *   a node         `step = lfo->out'. The same, one token shorter.
 *   a bound number is offered as a *binding* and not as a number: what
 *                  moves it is the knob, and the thing to change is which
 *                  knob it is.
 */

#include <string>
#include <vector>

#include "PanelModel.h"
#include "thcGenEdit.h"

class thcScheduler;
class thcStage;
class thcPlugin;

class StagePanel
{
public:
    StagePanel (void);

    /* The piece as the file describes it and as it is playing. Both are
     * borrowed, and a panel is exactly as stale as they are: a load frees
     * every thcStage and re-describing replaces every authored string, and
     * the caller rebuilds the panel from the same load.
     *
     * Either may be NULL, and then there is no panel. The document alone is
     * not enough -- the plugin's registered params are what the rows are,
     * and the file names a plugin without holding one. */
    void setPiece (const thcGenEdit::Doc *doc, thcScheduler *sched);

    /* Which stage, by its place in the *scheduler's* chain -- the one index
     * a stage has across the wasm boundary, and so the one an intent can
     * carry (wasm/web/thinkweb.cpp says why at length). The document's own
     * numbering counts dsp nodes as stages and the scheduler's does not;
     * this converts, once, here.
     *
     * A caller holding a document index -- the desktop's window does --
     * converts with thcGenEdit::liveIndex, which is the same call the canvas
     * makes. */
    void setStage (int chain, int stage);

    int chain (void) const { return chain_; }
    int stage (void) const { return stage_; }

    /* The panel. Its title is the stage's name and its subtitle the
       plugin's spelling, because a panel over one stage of a dozen has to
       say which. False for a stage that is not there, one whose plugin did
       not load, and one with no parameters at all. */
    bool build (thPanel &out) const;

    /* What an edit of `row' to `valueText' means: a GEN_PARAM intent whose
     * `valueText' is the whole right-hand side a .gen line would carry and
     * whose `a' and `b' are the chain and the stage.
     *
     * See the header for what a shell is expected to hand in. Refuses a row
     * that is shown rather than offered, a value the param's type cannot
     * take -- `H4' is not a note, a quoted preset is not a preset -- and a
     * knob the piece does not declare. */
    thPanelResult propose (const string &row, const string &valueText,
                           thPanelEdit &out) const;

    /* The audible half: the running stage's params, set to what the intent
     * says, exactly as a load of the same line would have set them.
     *
     * The lasting half is the caller's, and has to come first: the panel is
     * built from the document, so a shell that poked the stage without
     * splicing the file would show the old value over the new sound. False
     * when the stage has gone, which is what a stale intent looks like. */
    bool deliver (const thPanelEdit &edit) const;

    /* What a .gen would have to write for `param' if it wrote the plugin's
     * default, and "" where there is none that can be written down.
     *
     * Public because adding a stage wants the same answer before there is a
     * stage to build a panel over: docs/GEN_FORMAT.md's writer spells every
     * registered param out, so that a piece survives a plugin's defaults
     * changing, and it has to spell them the way this describes them or a
     * freshly written stage would read back as something else.
     *
     * Empty for a preset and an instrument set, and the rule above is why
     * rather than in spite of. "Write every param" exists so a piece
     * survives a plugin's *defaults* changing -- and those two have no
     * default that can be written at all, because the only legal value is
     * the name of something this piece declares and a plugin has never
     * heard of the piece. Falling through to the numeric case would write
     * `from = 0;', which the loader rejects by name and line: a generated
     * stage that will not load is worse than an absent line the loader is
     * happy to default. A caller indexing by param index gets an empty
     * string and not a gap, because skipping one shifts every param after
     * it up a slot. */
    static string defaultText (const thcPlugin *plugin, int param);

    /* Nothing polls here, deliberately.
     *
     * A channel's arg and a piece's knob are objects that move behind a
     * panel, and their providers answer a cheap "what is this worth now".
     * A stage's param is a line in a file: it moves when somebody edits it,
     * and then the document it was read from has changed and the panel is
     * rebuilt. The one value that does move on its own -- a param read
     * through a knob -- is read from the knob at every build, so a shell
     * that rebuilds to check the shape has the new number in its hand
     * already. */

private:
    /* The scheduler's stage, or NULL. */
    thcStage *live (void) const;

    /* The document's stage, or NULL -- which is what a dsp node stage and a
       stale index both look like. */
    const thcGenEdit::Stage *authored (void) const;

    /* The right-hand side the file gives for `name', or the spelling of the
       plugin's default for a line the file does not have.
     *
       A default spelled out rather than left blank because a panel showing
       nothing for an unwritten param would be a panel that cannot say what
       the stage is doing -- and because editing one has to insert a whole
       line, which needs a unit on it if it is a duration. */
    string valueTextOf (const thcPlugin *plugin, int param) const;

    const thcGenEdit::Doc *doc_;
    thcScheduler *sched_;
    int chain_, stage_;
};

#endif /* STAGE_PANEL_H */
