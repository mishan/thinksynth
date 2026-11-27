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

#ifndef GTH_KEY_ROUTE_H
#define GTH_KEY_ROUTE_H

#include <functional>
#include <set>
#include <utility>

/* Somewhere a played key goes instead of the channel's instrument (the
   piece, docs/GUI.md 4), for the on-screen keys and the controller alike.
   `on' says whether it took a press; a key it took is let go through
   `off', whatever `on' would say by then, so a release follows the press. */
class gthKeyRoute
{
public:
    void set (std::function<bool (int, int, float)> on,
              std::function<void (int, int)> off)
    {
        on_ = std::move(on);
        off_ = std::move(off);
    }

    /* Whether the press went there, and not to the channel. */
    bool press (int chan, int note, float veloc)
    {
        if (!on_ || !on_(chan, note, veloc))
            return false;

        taken_.insert(std::make_pair(chan, note));
        return true;
    }

    /* Whether the release went there: it does for a press that did. */
    bool release (int chan, int note)
    {
        if (taken_.erase(std::make_pair(chan, note)) == 0)
            return false;

        off_(chan, note);
        return true;
    }

private:
    std::function<bool (int, int, float)> on_;
    std::function<void (int, int)> off_;
    std::set<std::pair<int, int> > taken_;     /* channel, key */
};

#endif /* GTH_KEY_ROUTE_H */
