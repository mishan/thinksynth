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

#include <algorithm>
#include <cmath>

#include "thhHandTrack.h"

/* The tips' gap over the hand's size, wrist to middle knuckle, at which
   close is 1 and 0. */
#define THH_HANDS_SHUT_GAP 0.2f
#define THH_HANDS_OPEN_GAP 1.2f

/* How sure the tracker has to be for its word on a new hand to count, and
   how far, as a share of the picture, one hand may have moved and still be
   the hand it was. */
#define THH_HANDS_SURE 0.8f
#define THH_HANDS_NEAR 0.25f

const char *const THH_HANDS_PROFILE =
    "device hands {\n"
    "    base = \"C3\";\n"
    "    left_pinch = note 0;\n"
    "    right_y = bend { range = 15; scale = pentatonic; smooth = 0.05; };\n"
    "    left_y = aux 3 { min = 1; max = 0; smooth = 0.03; };\n"
    "};\n";

/* The same unsnapped: the bend free across the same three octaves. */
const char *const THH_HANDS_FREE_PROFILE =
    "device hands {\n"
    "    base = \"C3\";\n"
    "    left_pinch = note 0;\n"
    "    right_y = bend { range = 36; smooth = 0.05; };\n"
    "    left_y = aux 3 { min = 1; max = 0; smooth = 0.03; };\n"
    "};\n";

const thhControls &
thhHandsControls ()
{
    static const thhControls c = {
        "hands",
        { "left_pinch", "right_pinch", "left_seen", "right_seen" },
        {
            { "left_x", false }, { "left_y", false }, { "left_close", false },
            { "right_x", false }, { "right_y", false }, { "right_close", false },
        },
    };

    return c;
}

namespace {

struct Seen
{
    float x, y, close;
    int   side;             /* the tracker's, sure enough: 0 left, 1 right;
                               else -1 */
};

/* As the player sees it: the camera's picture mirrored, height up. */
Seen
seen (const thhHandPoints &p)
{
    static const int PALM[] = { 0, 5, 9, 13, 17 };
    Seen s;
    float x = 0, y = 0;

    for (int i : PALM)
    {
        x += p.x[i];
        y += p.y[i];
    }
    s.x = 1 - x / 5;
    s.y = 1 - y / 5;

    const float size = std::hypot(p.x[9] - p.x[0], p.y[9] - p.y[0]);
    const float gap = size > 0 ? std::hypot(p.x[8] - p.x[4], p.y[8] - p.y[4]) / size
                               : THH_HANDS_OPEN_GAP;

    s.close = std::clamp((THH_HANDS_OPEN_GAP - gap) /
                         (THH_HANDS_OPEN_GAP - THH_HANDS_SHUT_GAP), 0.0f, 1.0f);

    /* Its word is for a mirrored picture, and the camera's isn't one. */
    s.side = p.sure < THH_HANDS_SURE ? -1 : p.side == "Left" ? 1
           : p.side == "Right" ? 0 : -1;
    return s;
}

}

void
thhHandTrack::update (const std::vector<thhHandPoints> &hands, double seconds,
                      bool connected, thhState &out)
{
    out = thhState();
    out.time = seconds;
    if (!connected)
    {
        tracks_[0] = tracks_[1] = Track();
        return;
    }
    out.connected = true;

    for (Track &t : tracks_)
        if (t.held && seconds - t.seen > LOST_SECONDS)
            t = Track();

    /* The two the tracker is surest of. */
    std::vector<thhHandPoints> two(hands);

    std::sort(two.begin(), two.end(), [](const thhHandPoints &a,
                                         const thhHandPoints &b) {
        return a.sure > b.sure;
    });
    two.resize(std::min<size_t>(two.size(), 2));

    std::vector<Seen> now;

    for (const auto &p : two)
    {
        const Seen s = seen(p);

        if (std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.close))
            now.push_back(s);
    }

    const auto dist = [](const Track &t, const Seen &s) {
        return std::hypot(t.x - s.x, t.y - s.y);
    };
    /* Each hand seen goes to the side whose held hand it moved least
       from, two of them keeping as many held hands as they can, and is
       that hand if it is near enough; a hand that is not is new, from
       nothing, so no pinch carries onto it to hold a note it isn't
       holding. */
    const auto moved = [&](int t, int i) {
        return tracks_[t].held ? dist(tracks_[t], now[i]) : 0.0f;
    };
    const auto near = [&](int t, int i) {
        return tracks_[t].held && dist(tracks_[t], now[i]) < THH_HANDS_NEAR;
    };
    int side[2] = { -1, -1 };
    bool kept[2] = { false, false };

    if (now.size() == 2)
    {
        const int keeps[2] = { near(0, 0) + near(1, 1), near(1, 0) + near(0, 1) };

        side[0] = keeps[0] != keeps[1] ? keeps[1] > keeps[0]
                : moved(0, 1) + moved(1, 0) < moved(0, 0) + moved(1, 1);
        side[1] = 1 - side[0];
        kept[0] = near(side[0], 0);
        kept[1] = near(side[1], 1);
        if (!kept[0] && !kept[1])
        {
            side[0] = now[0].x <= now[1].x ? 0 : 1;
            side[1] = 1 - side[0];
        }
    }
    else if (now.size() == 1)
    {
        for (int t = 0; t < 2; t++)
            if (near(t, 0) && (side[0] < 0 || moved(t, 0) < moved(side[0], 0)))
                side[0] = t;
        kept[0] = side[0] >= 0;
        if (!kept[0])
            side[0] = now[0].side >= 0 ? now[0].side : now[0].x < 0.5f ? 0 : 1;
    }

    for (size_t i = 0; i < now.size(); i++)
    {
        Track &t = tracks_[side[i]];

        if (!kept[i])
            t = Track();
        t.held = true;
        t.seen = seconds;
        t.x = now[i].x;
        t.y = now[i].y;
        t.close = now[i].close;
        if (!t.pinched && t.close > PINCH_ON)
            t.pinched = true;
        else if (t.pinched && t.close < PINCH_OFF)
            t.pinched = false;
    }

    static const int AXIS[2] = { THH_HANDS_LEFT_X, THH_HANDS_RIGHT_X };
    static const int PINCH[2] = { THH_HANDS_LEFT_PINCH, THH_HANDS_RIGHT_PINCH };
    static const int SEEN[2] = { THH_HANDS_LEFT_SEEN, THH_HANDS_RIGHT_SEEN };

    for (int t = 0; t < 2; t++)
    {
        if (!tracks_[t].held)
            continue;
        out.axis[AXIS[t]] = tracks_[t].x;
        out.axis[AXIS[t] + 1] = tracks_[t].y;
        out.axis[AXIS[t] + 2] = tracks_[t].close;
        out.buttons |= 1u << SEEN[t];
        if (tracks_[t].pinched)
            out.buttons |= 1u << PINCH[t];
    }
}
