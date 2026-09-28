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

#ifndef STAGE_PARAMS_VIEW_H
#define STAGE_PARAMS_VIEW_H

#include "StagePanel.h"
#include "PanelView.h"

/* One composer stage's parameters, on the desktop: StagePanel drawn by
 * PanelView.
 *
 * Thin, like NodeParamsView and for the same half of the reason: what an
 * edit lasts as is a splice into the .gen, and the window owns that -- it
 * has the work copy, the dirty flag and the reload. The other half is not
 * the window's at all, and is why this reports the whole intent rather than
 * a number: what the running stage has to be told is `@warmth' or `4 beats'
 * or a resolved note list, and StagePanel::deliver is what knows that on
 * both platforms.
 *
 * So the window is handed an intent, splices it, and calls deliver. The
 * order matters and the header of StagePanel.h says why: the panel is built
 * from the document, so a stage poked before its line was written would show
 * the old value over the new sound.
 *
 * This is what ComposerWindow::addParamRow became. What left with it is
 * everything that turned out to be a rule rather than a drawing: which of
 * the three units a duration is written in, which knobs a param may be read
 * through, what a typed note set or preset name is allowed to be, and the
 * fact that a value the file works out with arithmetic is not a value
 * anybody may type over.
 */
class StageParamsView : public PanelView
{
public:
    StageParamsView (void);

    /* Shows a stage's parameters. `stage' is the scheduler's index, which is
       what StagePanel names one by; a negative chain clears the panel. */
    void setStage (const thcGenEdit::Doc *doc, thcScheduler *sched,
                   int chain, int stage);

    /* An edit the provider allowed, for the window to splice and deliver.
       Carries the chain, the stage, the param's name and the whole
       right-hand side a .gen line would have. */
    typedef sigc::signal<void(const thPanelEdit &)> type_signal_param_edited;

    type_signal_param_edited signal_param_edited (void)
    {
        return signal_param_edited_;
    }

    /* Why the last edit was refused, or "" -- for the window's status line,
       which is the only place a person would look. */
    const string &why (void) const { return why_; }

    StagePanel &model (void) { return model_; }

private:
    void onEdited (const string &row, const string &valueText);

    StagePanel model_;
    string why_;
    type_signal_param_edited signal_param_edited_;
};

#endif /* STAGE_PARAMS_VIEW_H */
