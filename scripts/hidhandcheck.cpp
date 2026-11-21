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

/*
 * hidhandcheck -- a hand tracker's hands as a controller (hid/thhHandTrack.h).
 *
 * Each row is frames, each a time and the hands in it: where the palm is,
 * as the player sees it, how far apart the thumb's and index finger's tips
 * are over the hand's size, and what the tracker called it, and how sure.
 * What each frame gave is the hands it says, "L" and "R", each at its x and
 * height, its close, and "+" while pinched; "-" for a hand not seen.
 */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "thHidBlock.h"
#include "thhHandTrack.h"
#include "thhMapper.h"

using std::string;

static int failed = 0;

static void
report (const char *name, const string &got, const string &want)
{
    if (got == want)
        printf("ok    %s\n", name);
    else
    {
        printf("FAIL  %s\n  got  %s\n  want %s\n", name, got.c_str(), want.c_str());
        failed++;
    }
}

struct Hand
{
    float x, y;         /* the palm, as the player sees it: 0..1 across, up */
    float gap;          /* the tips apart, over the hand's size */
    const char *side;
    float sure;
};

struct Frame
{
    double at;
    std::vector<Hand> hands;
    bool gone;
};

/* A hand's 21 points in the camera's picture: the palm where `h' says,
   mirrored, a tenth of the picture high, the tips `gap' apart. */
static thhHandPoints
points (const Hand &h)
{
    const float s = 0.1f, cx = 1 - h.x, cy = 1 - h.y;
    thhHandPoints p = thhHandPoints();

    for (int i = 0; i < 21; i++)
    {
        p.x[i] = cx;
        p.y[i] = cy - 0.3f * s;
    }
    p.y[0] = cy + 0.4f * s;
    for (int k = 0; k < 4; k++)
    {
        p.x[5 + 4 * k] = cx + (k - 1.5f) * 0.1f * s;
        p.y[5 + 4 * k] = cy - 0.1f * s;
    }

    const float size = std::hypot(p.x[9] - p.x[0], p.y[9] - p.y[0]);

    p.x[4] = cx - h.gap * size / 2;
    p.x[8] = cx + h.gap * size / 2;
    p.y[4] = p.y[8] = cy - 0.5f * s;
    p.side = h.side;
    p.sure = h.sure;
    return p;
}

static string
describe (const thhState &s)
{
    static const struct { char name; int x, y, close, pinch, seen; } hands[] = {
        { 'L', THH_HANDS_LEFT_X, THH_HANDS_LEFT_Y, THH_HANDS_LEFT_CLOSE,
          THH_HANDS_LEFT_PINCH, THH_HANDS_LEFT_SEEN },
        { 'R', THH_HANDS_RIGHT_X, THH_HANDS_RIGHT_Y, THH_HANDS_RIGHT_CLOSE,
          THH_HANDS_RIGHT_PINCH, THH_HANDS_RIGHT_SEEN },
    };
    string out;

    if (!s.connected)
        return "gone";
    for (const auto &h : hands)
    {
        char one[64];

        if (s.buttons & (1u << h.seen))
            snprintf(one, sizeof(one), "%c %.2f %.2f %.1f%s", h.name, s.axis[h.x],
                     s.axis[h.y], s.axis[h.close],
                     s.buttons & (1u << h.pinch) ? "+" : "");
        else
            snprintf(one, sizeof(one), "%c -", h.name);
        out += (out.empty() ? "" : ", ") + string(one);
    }
    return out;
}

static string
play (const std::vector<Frame> &frames, double *time = NULL)
{
    thhHandTrack track;
    string out;

    for (const Frame &f : frames)
    {
        std::vector<thhHandPoints> hands;
        thhState s;

        for (const Hand &h : f.hands)
            hands.push_back(points(h));
        track.update(hands, f.at, !f.gone, s);
        out += (out.empty() ? "" : " | ") + describe(s);
        if (time)
            *time = s.time;
    }
    return out;
}

#define OPEN 1.2f
#define SHUT 0.1f

static const struct
{
    const char *name;
    std::vector<Frame> frames;
    const char *want;
} ROWS[] = {
    { "one hand, nothing sure, is the side of the picture it is on",
      { { 0, { { 0.3f, 0.6f, OPEN, "Left", 0.5f } }, false } },
      "L 0.30 0.60 0.0, R -" },
    { "one hand the tracker is sure of is what it says, mirrored",
      { { 0, { { 0.3f, 0.6f, OPEN, "Left", 0.9f } }, false } },
      "L -, R 0.30 0.60 0.0" },
    { "two hands are left and right as they lie, whatever they're called",
      { { 0, { { 0.7f, 0.2f, OPEN, "Right", 0.9f },
               { 0.3f, 0.8f, OPEN, "Right", 0.9f } }, false } },
      "L 0.30 0.80 0.0, R 0.70 0.20 0.0" },
    { "and keep their sides, one above the other, as they pass across",
      { { 0.00, { { 0.48f, 0.3f, OPEN, "", 0 }, { 0.52f, 0.7f, OPEN, "", 0 } }, false },
        { 0.03, { { 0.53f, 0.3f, OPEN, "", 0 }, { 0.50f, 0.7f, OPEN, "", 0 } }, false } },
      "L 0.48 0.30 0.0, R 0.52 0.70 0.0 | L 0.53 0.30 0.0, R 0.50 0.70 0.0" },
    { "one of two, the other gone, is the nearer of them",
      { { 0.00, { { 0.3f, 0.5f, OPEN, "", 0 }, { 0.7f, 0.5f, OPEN, "", 0 } }, false },
        { 0.03, { { 0.65f, 0.6f, OPEN, "Left", 0.9f } }, false } },
      "L 0.30 0.50 0.0, R 0.70 0.50 0.0 | L 0.30 0.50 0.0, R 0.65 0.60 0.0" },
    { "a hand gone a moment keeps its values; gone longer, it is gone",
      { { 0.00, { { 0.3f, 0.5f, SHUT, "", 0 } }, false },
        { 0.10, {}, false },
        { 0.20, {}, false } },
      "L 0.30 0.50 1.0+, R - | L 0.30 0.50 1.0+, R - | L -, R -" },
    { "a pinch closes past 0.75 and opens under 0.5",
      { { 0.00, { { 0.3f, 0.5f, 0.6f, "", 0 } }, false },
        { 0.03, { { 0.3f, 0.5f, 0.4f, "", 0 } }, false },
        { 0.06, { { 0.3f, 0.5f, 0.6f, "", 0 } }, false },
        { 0.09, { { 0.3f, 0.5f, 0.8f, "", 0 } }, false } },
      "L 0.30 0.50 0.6, R - | L 0.30 0.50 0.8+, R - | L 0.30 0.50 0.6+, R - | "
      "L 0.30 0.50 0.4, R -" },
    { "a sure Right is the player's left, mirrored",
      { { 0, { { 0.7f, 0.5f, OPEN, "Right", 0.9f } }, false } },
      "L 0.70 0.50 0.0, R -" },
    { "of three hands, the two the tracker is surest of",
      { { 0, { { 0.2f, 0.5f, OPEN, "", 0.3f }, { 0.5f, 0.5f, OPEN, "", 0.9f },
               { 0.8f, 0.5f, OPEN, "", 0.95f } }, false } },
      "L 0.50 0.50 0.0, R 0.80 0.50 0.0" },
    { "a hand held keeps its own, pinch and all, when another comes",
      { { 0.00, { { 0.45f, 0.5f, SHUT, "", 0 } }, false },
        { 0.03, { { 0.30f, 0.5f, OPEN, "", 0 }, { 0.47f, 0.5f, SHUT, "", 0 } }, false } },
      "L 0.45 0.50 1.0+, R - | L 0.47 0.50 1.0+, R 0.30 0.50 0.0" },
    { "a hand held keeps its own though swapping both would move them less",
      { { 0.00, { { 0.2f, 0.2f, SHUT, "", 0 }, { 0.8f, 0.2f, OPEN, "", 0 } }, false },
        { 0.03, { { 0.4f, 0.2f, 0.6f, "", 0 }, { 0.1f, 0.7f, OPEN, "", 0 } }, false } },
      "L 0.20 0.20 1.0+, R 0.80 0.20 0.0 | L 0.40 0.20 0.6+, R 0.10 0.70 0.0" },
    { "a hand far from the one held is new",
      { { 0.00, { { 0.3f, 0.5f, OPEN, "", 0 } }, false },
        { 0.03, { { 0.8f, 0.5f, OPEN, "", 0 } }, false } },
      "L 0.30 0.50 0.0, R - | L 0.30 0.50 0.0, R 0.80 0.50 0.0" },
    { "two hands far from the one held are new, as they lie",
      { { 0.00, { { 0.4f, 0.1f, OPEN, "", 0 } }, false },
        { 0.03, { { 0.1f, 0.9f, OPEN, "", 0 }, { 0.4f, 0.9f, OPEN, "", 0 } }, false } },
      "L 0.40 0.10 0.0, R - | L 0.10 0.90 0.0, R 0.40 0.90 0.0" },
    { "a new hand where a pinched one was doesn't carry its pinch",
      { { 0.00, { { 0.1f, 0.2f, SHUT, "", 0 } }, false },
        { 0.03, { { 0.4f, 0.8f, 0.6f, "", 0 } }, false } },
      "L 0.10 0.20 1.0+, R - | L 0.40 0.80 0.6, R -" },
    { "a hand that isn't numbers isn't a hand",
      { { 0, { { NAN, 0.5f, OPEN, "", 0 } }, false } },
      "L -, R -" },
    { "a camera gone forgets the hands",
      { { 0.00, { { 0.3f, 0.5f, SHUT, "", 0 } }, false },
        { 0.03, {}, true },
        { 0.06, { { 0.7f, 0.5f, OPEN, "", 0 } }, false } },
      "L 0.30 0.50 1.0+, R - | gone | L -, R 0.70 0.50 0.0" },
};

int
main (void)
{
    for (const auto &r : ROWS)
        report(r.name, play(r.frames), r.want);

    double time = 0;

    play({ { 1.25, { { 0.3f, 0.5f, OPEN, "", 0 } }, false } }, &time);
    report("a state says when it was", std::to_string(time), std::to_string(1.25));

    thHidBlock profile;
    thhMapper mapper;
    std::vector<thhAction> acts;
    string err;
    int line = 0;
    const bool bound = thHidParseProfile(THH_HANDS_PROFILE, profile, err, line) &&
                       mapper.configure(profile, thhHandsControls(), acts, err, line);

    report("the hands' own profile binds", bound ? "" : err, "");

    {
        thHidBlock free;
        thhMapper m;

        report("and so does its unsnapped one",
               thHidParseProfile(THH_HANDS_FREE_PROFILE, free, err, line) &&
               m.configure(free, thhHandsControls(), acts, err, line) ? "" : err, "");
    }

    /* Played: the left hand pinched 0.8 up, the right a fifth of
       the way up, a tenth of a second at a time, then the left gone. */
    thhHandTrack track;
    string notes;
    double bend = 0, aux = 0;

    for (int f = 0; f < 12; f++)
    {
        std::vector<thhHandPoints> hands = { points({ 0.7f, 0.2f, OPEN, "", 0 }) };
        thhState s;

        if (f < 8)
            hands.push_back(points({ 0.3f, 0.8f, SHUT, "", 0 }));
        acts.clear();
        track.update(hands, f * 0.1, true, s);
        mapper.update(s, 0, acts);
        for (const thhAction &a : acts)
            if (a.kind == thhAction::NOTE_ON || a.kind == thhAction::NOTE_OFF)
                notes += (notes.empty() ? "" : ", ") +
                         string(a.kind == thhAction::NOTE_ON ? "on " : "off ") +
                         std::to_string(a.note);
            else if (a.kind == thhAction::NOTE_BEND)
                bend = a.t;
            else if (a.kind == thhAction::NOTE_AUX && a.aux == 3)
                aux = a.t;
    }
    report("a left pinch plays C3 and letting the hand go ends it", notes,
           "on 48, off 48");
    report("the right hand's height bends it to the scale's step there",
           std::to_string(std::lround(bend * 100)), "700");
    report("how far the left hand is below the top is its aux3",
           std::to_string(std::lround(aux * 100)), "20");

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed;
}
