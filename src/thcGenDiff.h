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

#ifndef THCGENDIFF_H
#define THCGENDIFF_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "thcGenEdit.h"
#include "thcScheduler.h"

class thcPlugin;

/* A piece's text replaced while it plays: what survives, and the edit
 * itself.
 *
 * The rule, which every peer applies to the same two texts at the same
 * transport time and so to the same effect:
 *
 *   - Stages are named by chain and stage name. A stage whose text is the
 *     same in both -- category, plugin, and every param as authored -- keeps
 *     its instance: its state, its next wake, its bindings.
 *   - A stage that changed, or is new, or has no name to be found by, is
 *     created from the new text with the seed its place there gives it.
 *     One that is gone is destroyed.
 *   - A chain with embedded nodes is kept whole or rebuilt whole: its stages
 *     read the node host by pointer (thcScheduler::adopt).
 *   - A changed seed, scale or preset keeps nothing. Each can change what an
 *     unchanged line means, and a stage whose text is the same and whose
 *     meaning is not is the one case the rule must not guess at.
 *   - A knob whose declaration is unchanged keeps the value it has been
 *     moved to; one whose declaration changed takes the new text's.
 *   - An instrument whose declaration or files changed is loaded again,
 *     which cuts what is sounding on its channel. One that did not is left
 *     alone.
 *   - What was already composed is not taken back: notes queued for later,
 *     and the offs of notes sounding, are delivered as they would have been.
 */
class thcGenDiff
{
public:
    /* What `newPath' keeps of `oldPath'. `changedFiles' names the .dsp
       files the edit changed as well. False with `why' if either text
       cannot be read. */
    static bool plan (const std::string &oldPath, const std::string &newPath,
                      const std::set<std::string> &changedFiles,
                      thcScheduler::EditPlan &plan,
                      std::set<std::string> &keepKnobValues,
                      std::string &why);

    /* The edit, at the transport's time now: `newPath' read into a staged
       scheduler, and adopted by `live' if it loads. `oldPath' is the text
       `live' is playing -- including any edit made to it since it was
       loaded, since that is what its stages are. A piece that pins no
       seed goes on with the one `live' has.

       False with `errors' when the new text does not load, and then
       nothing has changed; false as well when it loaded and an instrument
       did not, and then the edit has been applied without it. */
    static bool apply (thcScheduler &live,
                       const std::map<std::string, thcPlugin *> &plugins,
                       const std::string &oldPath, const std::string &newPath,
                       const std::set<std::string> &changedFiles,
                       std::vector<std::string> &errors);
};

#endif /* THCGENDIFF_H */
