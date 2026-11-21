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
 * phonecheck -- a phone's frames as a controller (hid/thhPhone.h), and its
 * own profile played.
 *
 * Which frames are taken: in order within a page load, any from a new one,
 * across the sequence's wrap, none malformed, and a value past its range
 * held to it. When the phone is gone: a second without a frame, not a
 * heartbeat lost. THH_PHONE_PROFILE through the mapper: a finger plays at
 * its own pitch and loudness from the start, an octave a tap away and
 * back. And what the mapper sends a note started with something else in
 * the same moment: a glide handed on, a layer switched to.
 */

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "thHidBlock.h"
#include "thhMapper.h"
#include "thhPhone.h"

using std::string;

static int failed = 0;

static void
check (bool good, const string &what, const string &detail = "")
{
    if (good)
        printf("ok    %s\n", what.c_str());
    else
    {
        printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ",
               detail.c_str());
        failed++;
    }
}

/* A frame from page load `id': one finger at (x, y) if `down', the phone
   level. */
static thhPhoneFrame
frame (uint32_t id, uint32_t seq, bool down = false, float x = 0, float y = 0,
       float tilt = 0)
{
    thhPhoneFrame f = {};

    f.id = id;
    f.seq = seq;
    f.axes[THH_PHONE_TILT_X] = tilt;
    f.axes[THH_PHONE_TOUCH_1_X] = x;
    f.axes[THH_PHONE_TOUCH_1_Y] = y;
    f.down = down ? 1u << THH_PHONE_TOUCH_1 : 0;
    return f;
}

static void
checkTaken ()
{
    const struct
    {
        const char *what;
        std::vector<thhPhoneFrame> frames;
        string want;            /* a + for each frame taken, - each not */
    } rows[] = {
        { "frames in order are taken", { frame(1, 1), frame(1, 2), frame(1, 5) },
          "+++" },
        { "an older or the same one is not",
          { frame(1, 5), frame(1, 4), frame(1, 5), frame(1, 6) }, "+--+" },
        { "a new page load starts afresh, wherever its count is",
          { frame(1, 50), frame(2, 1), frame(2, 2) }, "+++" },
        { "the count wraps",
          { frame(1, 0xfffffffeu), frame(1, 0xffffffffu), frame(1, 0), frame(1, 1) },
          "++++" },
    };

    for (const auto &r : rows)
    {
        thhPhone phone;
        string got;

        for (size_t i = 0; i < r.frames.size(); i++)
            got += phone.take(r.frames[i], i * 0.01) ? "+" : "-";
        check(got == r.want, r.what, got);
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const struct
    {
        const char *what;
        int axis;
        float value;
        uint32_t down;
    } bad[] = {
        { "a value that isn't a number", THH_PHONE_TOUCH_1_X, nan, 0 },
        { "the fifth finger's, not a number",
          THH_PHONE_TOUCH_1_Y + 2 * (THH_PHONE_BUTTONS - 1), nan, 0 },
        { "an infinite one", THH_PHONE_TILT_X, -INFINITY, 0 },
        { "a sixth finger", THH_PHONE_TILT_X, 0, 1u << THH_PHONE_BUTTONS },
    };
    unsigned refused = 0;
    thhPhone phone;

    phone.take(frame(1, 1, true, 0.5f, 0.5f), 0);
    for (const auto &b : bad)
    {
        thhPhoneFrame f = frame(1, 2);

        f.axes[b.axis] = b.value;
        f.down = b.down;
        refused += phone.take(f, 0.01) ? 0 : 1;
    }
    check(refused == sizeof(bad) / sizeof(bad[0]) &&
          phone.dropped() == refused &&
          phone.state(0.02).buttons == 1u << THH_PHONE_TOUCH_1,
          "a malformed frame is dropped and changes nothing");

    /* Past the range, held to it: a shaken phone's gravity is more than
       gravity, and a finger sliding off the screen is off it. */
    thhPhoneFrame over = frame(1, 3, true, 1.2f, -0.1f, -1.5f);

    over.axes[THH_PHONE_TILT_Y] = 1.3f;
    over.axes[THH_PHONE_TOUCH_1_Y + 2 * (THH_PHONE_BUTTONS - 1)] = 0.4f;

    const thhState held = (phone.take(over, 0.03), phone.state(0.03));

    check(held.axis[THH_PHONE_TILT_X] == -1 && held.axis[THH_PHONE_TILT_Y] == 1 &&
          held.axis[THH_PHONE_TOUCH_1_X] == 1 &&
          held.axis[THH_PHONE_TOUCH_1_Y] == 0 &&
          held.axis[THH_PHONE_TOUCH_1_Y + 2 * (THH_PHONE_BUTTONS - 1)] == 0.4f,
          "a value past its range is held to it, the fifth finger's taken as it is");
}

static void
checkGone ()
{
    thhPhone phone;

    check(!phone.state(0).connected, "a phone that has sent nothing isn't there");

    phone.take(frame(1, 1, true, 0.3f, 0.6f), 10);
    {
        const thhState s = phone.state(10.5);

        check(s.connected && s.buttons == 1u << THH_PHONE_TOUCH_1 &&
              s.axis[THH_PHONE_TOUCH_1_X] == 0.3f && s.time == 10.5,
              "a frame is the phone's state until the next");
    }
    check(phone.state(10.99).connected,
          "a heartbeat or two lost is nothing");
    check(!phone.state(11.0).connected && phone.state(11.0).buttons == 0,
          "a second without a frame is the phone gone, holding nothing");

    phone.take(frame(1, 2, true, 0.3f, 0.6f), 11.2);
    check(phone.state(11.2).connected, "and the next frame brings it back");
}

/* A profile played, a frame at a time: `frames' as (seconds, frame), and
   every action of each. */
static std::vector<std::vector<thhAction>>
play (thhMapper &m, thhPhone &phone,
      const std::vector<std::pair<double, thhPhoneFrame>> &frames)
{
    std::vector<std::vector<thhAction>> played;

    for (const auto &f : frames)
    {
        std::vector<thhAction> acts;

        phone.take(f.second, f.first);
        m.update(phone.state(f.first), 0, acts);
        played.push_back(acts);
    }
    return played;
}

static bool
bind (const char *text, thhMapper &m)
{
    thHidBlock profile;
    std::vector<thhAction> acts;
    string err;
    int line = 0;

    if (thHidParseProfile(text, profile, err, line) &&
        m.configure(profile, thhPhoneControls(), acts, err, line))
        return true;
    check(false, "a profile binds", err);
    return false;
}

static const thhAction *
find (const std::vector<thhAction> &acts, thhAction::Kind kind, int aux = -1)
{
    for (const thhAction &a : acts)
        if (a.kind == kind && (aux < 0 || a.aux == aux))
            return &a;
    return NULL;
}

static void
checkProfile ()
{
    thHidBlock profile;
    std::vector<thhAction> acts;
    string err;
    int line = 0;
    thhMapper m;

    if (!thHidParseProfile(THH_PHONE_PROFILE, profile, err, line) ||
        !m.configure(profile, thhPhoneControls(), acts, err, line))
    {
        check(false, "the phone's own profile binds", err);
        return;
    }
    check(true, "the phone's own profile binds");

    thhPhone phone;
    std::vector<std::pair<double, thhPhoneFrame>> frames;
    uint32_t seq = 0;
    double t = 0;
    const auto at = [&](double seconds, bool down, float x, float y,
                        float tilt = 0)
    {
        for (; t < seconds; t += 1.0 / 60)
            frames.push_back({ t, frame(1, ++seq, down, x, y, tilt) });
    };

    /* A finger at 0.2 across, high, held a second; lifted; then down at
       0.8 across. */
    at(1.0, true, 0.2f, 0.9f, 0.5f);
    const size_t lifted = frames.size();
    at(1.5, false, 0.2f, 0.9f);
    const size_t landed = frames.size();
    at(2.5, true, 0.8f, 0.9f);

    const auto played = play(m, phone, frames);
    const thhAction *on = find(played[0], thhAction::NOTE_ON);
    const thhAction *off = find(played[lifted], thhAction::NOTE_OFF);

    check(on && on->note == 48 && off && off->note == 48,
          "a finger plays C3 and lifting it ends it");

    double settledAt02 = 0, settledAt08 = 0, loud = 0, bright = 0;

    for (size_t i = 0; i < lifted; i++)
    {
        if (const thhAction *b = find(played[i], thhAction::NOTE_BEND))
            settledAt02 = b->t;
        if (const thhAction *a = find(played[i], thhAction::NOTE_AUX, 3))
            loud = a->t;
        if (const thhAction *a = find(played[i], thhAction::NOTE_AUX, 1))
            bright = a->t;
    }
    for (size_t i = landed; i < played.size(); i++)
        if (const thhAction *b = find(played[i], thhAction::NOTE_BEND))
            settledAt08 = b->t;
    check(std::lround(settledAt02) == 7 && std::lround(settledAt08) == 28,
          "across the screen is the pentatonic scale: a fifth of the way a "
          "fifth up, four fifths two octaves and a third",
          std::to_string(settledAt02) + ", " + std::to_string(settledAt08));
    check(std::fabs(loud - 0.1) < 0.01,
          "high on the screen is loud: the hush, aux3, near 0",
          std::to_string(loud));
    check(std::fabs(bright - 0.75) < 0.01,
          "tilted halfway right is three quarters of aux1",
          std::to_string(bright));

    /* The second note's first bend is its own finger's place, not a glide
       from where the last one was, and so is its loudness. */
    const thhAction *again = find(played[landed], thhAction::NOTE_ON);
    const thhAction *first = find(played[landed], thhAction::NOTE_BEND);
    const thhAction *hush = find(played[landed], thhAction::NOTE_AUX, 3);

    check(again && first && std::fabs(first->t - settledAt08) < 0.5 && hush &&
          std::fabs(hush->t - 0.1) < 0.01,
          "a finger landing plays at its own pitch and loudness from the start",
          (first ? std::to_string(first->t) : "no bend") + ", " +
          (hush ? std::to_string(hush->t) : "no aux3"));
}

/* What a note started in the same moment as something else is sent. */
static void
checkMoments ()
{
    {
        /* One finger lifting as the next lands, mid-glide: the glide goes
           on into the second note rather than jumping to its end. */
        thhMapper m;
        thhPhone phone;

        if (!bind("device phone {\n"
                  "    touch_1 = note 0;\n"
                  "    touch_2 = note 2;\n"
                  "    tilt_x = bend { range = 12; smooth = 0.5; };\n"
                  "};\n", m))
            return;

        thhPhoneFrame f = frame(1, 1);

        f.axes[THH_PHONE_TILT_X] = -1;
        f.down = 1u << THH_PHONE_TOUCH_1;
        play(m, phone, { { 0, f } });
        f.seq = 2;
        f.axes[THH_PHONE_TILT_X] = 1;
        play(m, phone, { { 0.1, f } });
        f.seq = 3;
        f.down = 1u << THH_PHONE_TOUCH_2;

        const auto acts = play(m, phone, { { 0.2, f } });
        const thhAction *b = find(acts[0], thhAction::NOTE_BEND);

        check(find(acts[0], thhAction::NOTE_ON) && b && b->t > -12 && b->t < 6,
              "a note handed on in the same moment keeps the glide going",
              b ? std::to_string(b->t) : "no bend");
    }

    {
        /* A layer held down in the same moment as a note: the note is
           bent as the layer bends, from its first moment. */
        thhMapper m;
        thhPhone phone;

        if (!bind("device phone {\n"
                  "    touch_1 = note 0;\n"
                  "    touch_2 = hold up;\n"
                  "    tilt_x = bend { range = 6; };\n"
                  "    layer up {\n"
                  "        tilt_x = bend { range = 12; };\n"
                  "    };\n"
                  "};\n", m))
            return;

        thhPhoneFrame f = frame(1, 1);

        f.axes[THH_PHONE_TILT_X] = 1;
        f.down = 1u << THH_PHONE_TOUCH_1 | 1u << THH_PHONE_TOUCH_2;

        const auto acts = play(m, phone, { { 0, f } });
        const thhAction *b = find(acts[0], thhAction::NOTE_BEND);

        check(b && std::fabs(b->t - 12) < 0.01,
              "a layer switched to in a note's own moment bends it from the start",
              b ? std::to_string(b->t) : "no bend");
    }
}

/* The second and third fingers: an octave up and down, as often as
   tapped, and back. */
static void
checkOctaves ()
{
    thHidBlock profile;
    std::vector<thhAction> acts;
    string err;
    int line = 0;
    thhMapper m;
    thhPhone phone;
    uint32_t seq = 0;
    double t = 0;

    thHidParseProfile(THH_PHONE_PROFILE, profile, err, line);
    m.configure(profile, thhPhoneControls(), acts, err, line);

    const auto tap = [&](int finger)
    {
        for (int i = 0; i < 2; i++)
        {
            thhPhoneFrame f = frame(1, ++seq);

            f.down = i == 0 ? 1u << finger : 0;
            std::vector<thhAction> a;

            phone.take(f, t += 0.05);
            m.update(phone.state(t), 0, a);
        }
    };
    const auto note = [&]()
    {
        std::vector<thhAction> a;
        int n = -1;

        phone.take(frame(1, ++seq, true, 0, 0.5f), t += 0.05);
        m.update(phone.state(t), 0, a);
        if (const thhAction *on = find(a, thhAction::NOTE_ON))
            n = on->note;
        phone.take(frame(1, ++seq), t += 0.05);
        m.update(phone.state(t), 0, a);
        return n;
    };

    tap(THH_PHONE_TOUCH_2);
    tap(THH_PHONE_TOUCH_2);

    const int up2 = note();

    tap(THH_PHONE_TOUCH_3);
    tap(THH_PHONE_TOUCH_3);
    check(up2 == 72 && note() == 48,
          "two taps of the second finger are two octaves up, and two of the "
          "third bring it back",
          std::to_string(up2));
}

int
main (void)
{
    checkTaken();
    checkGone();
    checkProfile();
    checkOctaves();
    checkMoments();

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");
    return failed;
}
