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
 * hidcheck -- what the Index controller's reports mean, held still.
 *
 * scripts/hid/index-input.txt is a capture, one report per line behind the
 * gesture that was being made, trimmed to each gesture's extremes and an
 * even sample of the rest. Each row below names a gesture and what decoding
 * its reports must show:
 *
 *   on      buttons that go down at some point. No other click (a button
 *           not named _touch) may; touches come and go with the hand and
 *           are only checked where named.
 *   peak    the grip zone that reaches highest, for the curls.
 *   reach   axis>=v, axis<=v: the axis gets that far; axis=v: it never
 *           leaves v.
 *
 * Then what isn't an input report, one report built by hand whose every
 * field is known, and which of the controller's report descriptors
 * (scripts/hid/index-descriptors.txt) is the input interface's.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "thhIndex.h"

using std::string;

static int failed = 0;

static void
report (const string &name, const string &why)
{
    if (why.empty())
        printf("ok    %s\n", name.c_str());
    else
    {
        printf("FAIL  %s\n  %s\n", name.c_str(), why.c_str());
        failed++;
    }
}

struct Gesture
{
    const char *label, *on, *peak, *reach;
};

static const Gesture GESTURES[] = {
    { "rest, hand open",            "", NULL,
      "grip_top=0 grip_low=0 trigger=0 pad_x=0 stick_x=0" },
    { "curl index only",            "", "grip_top", "" },
    { "curl middle only",           "", "grip_mid", "" },
    { "curl ring only",             "", "grip_low", "" },
    { "curl pinky only",            "", "grip_low", "" },
    { "fist, light",                "trigger_touch", NULL,
      "trigger_near>=0.9 grip_low>=0.9" },
    { "fist, squeeze hard",         "", NULL, "grip_force>=1" },
    { "trigger pull slowly",        "trigger_touch", NULL,
      "trigger>=1 trigger<=0" },
    { "trigger click",              "trigger_click", NULL, "trigger>=1" },
    { "touch trackpad, circle",     "trackpad_touch", NULL,
      "pad_x>=0.25 pad_x<=-0.45 pad_y>=0.55 pad_y<=-0.85 stick_x=0" },
    { "press trackpad, light then hard", "trackpad_touch", NULL,
      "pad_force>=1" },
    { "thumbstick, circle",         "thumbstick_touch", NULL,
      "stick_x>=0.99 stick_x<=-0.99 stick_y>=0.99 stick_y<=-0.85 pad_x=0" },
    { "press thumbstick",           "thumbstick", NULL, "" },
    { "press A",                    "a a_touch", NULL, "" },
    { "press B",                    "b b_touch", NULL, "" },
    { "press system button",        "system system_touch", NULL, "" },
};

static std::vector<uint8_t>
unhex (const string &hex)
{
    std::vector<uint8_t> out;

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), NULL, 16));
    return out;
}

static bool
has (const string &list, const string &word)
{
    std::istringstream w(list);
    string x;

    while (w >> x)
        if (x == word)
            return true;
    return false;
}

static void
checkCapture (const char *path)
{
    const thhControls &c = thhIndexControls();
    std::map<string, std::vector<thhState> > seen;
    std::ifstream in(path);
    string line;
    int pings = 0, refused = 0;

    if (!in)
    {
        report("the capture", string("can't read ") + path);
        return;
    }

    while (std::getline(in, line))
    {
        const size_t tab = line.find('\t');
        const std::vector<uint8_t> b = unhex(line.substr(tab + 1));
        thhState s;

        s.connected = true;
        if (b.size() > 3 && b[2] == 0x04 && b[3] == 0x0b)
            pings++;
        if (thhIndexFeed(b.data(), b.size(), s))
            seen[line.substr(0, tab)].push_back(s);
        else
            refused++;
    }

    report("the capture's pings are not input",
           refused == pings && pings > 0 ? "" :
           std::to_string(refused) + " refused, " + std::to_string(pings) +
           " pings");

    for (size_t g = 0; g < sizeof(GESTURES) / sizeof(GESTURES[0]); g++)
    {
        const Gesture &r = GESTURES[g];
        const std::vector<thhState> &ss = seen[r.label];
        string why;

        if (ss.empty())
        {
            report(r.label, "no reports in the capture");
            continue;
        }

        for (size_t b = 0; b < c.buttons.size(); b++)
        {
            bool down = false;

            for (size_t i = 0; i < ss.size(); i++)
                down = down || (ss[i].buttons & (1u << b));

            const string &n = c.buttons[b];
            const bool click = n.size() < 6 ||
                               n.compare(n.size() - 6, 6, "_touch") != 0;

            if (has(r.on, c.buttons[b]) && !down)
                why += c.buttons[b] + " never went down; ";
            else if (!has(r.on, c.buttons[b]) && click && down)
                why += c.buttons[b] + " went down; ";
        }

        std::vector<float> lo(c.axes.size(), 2), hi(c.axes.size(), -2);

        for (size_t i = 0; i < ss.size(); i++)
            for (size_t a = 0; a < c.axes.size(); a++)
            {
                lo[a] = std::min(lo[a], ss[i].axis[a]);
                hi[a] = std::max(hi[a], ss[i].axis[a]);
            }

        if (r.peak)
        {
            const char *zones[] = { "grip_top", "grip_mid", "grip_low",
                                    "trigger_near" };

            for (int z = 0; z < 4; z++)
                if (zones[z] != string(r.peak) &&
                    hi[c.axis(zones[z])] >= hi[c.axis(r.peak)])
                    why += string(zones[z]) + " reached as high as " +
                           r.peak + "; ";
        }

        std::istringstream reach(r.reach);
        string term;

        while (reach >> term)
        {
            const size_t op = term.find_first_of("<>=");
            const string name = term.substr(0, op);
            const bool ge = term.compare(op, 2, ">=") == 0;
            const bool le = term.compare(op, 2, "<=") == 0;
            const float v = (float)atof(term.c_str() + op + (ge || le ? 2 : 1));
            const int a = c.axis(name);
            char got[64];

            snprintf(got, sizeof(got), "%s went %.3f..%.3f; ", name.c_str(),
                     a < 0 ? 0 : lo[a], a < 0 ? 0 : hi[a]);
            if (a < 0 || (ge && hi[a] < v) || (le && lo[a] > v) ||
                (!ge && !le && (lo[a] != v || hi[a] != v)))
                why += string("wanted ") + term + ", " + got;
        }

        report(r.label, why);
    }
}

static string
describe (const thhState &s)
{
    const thhControls &c = thhIndexControls();
    std::ostringstream out;
    char v[16];

    for (size_t b = 0; b < c.buttons.size(); b++)
        if (s.buttons & (1u << b))
            out << c.buttons[b] << " ";
    out << "|";
    for (size_t a = 0; a < c.axes.size(); a++)
    {
        snprintf(v, sizeof(v), "%.3f", s.axis[a]);
        out << " " << c.axes[a].name << "=" << v;
    }
    return out.str();
}

static void
checkByHand (void)
{
    uint8_t r[64];
    thhState s, before;

    memset(r, 0, sizeof(r));
    r[0x00] = 0x01;
    r[0x02] = 0x07;  r[0x03] = 0x3c;
    r[0x08] = 0x05;                      /* trigger click, A */
    r[0x09] = 0x30;                      /* B, system */
    r[0x0a] = 0x14;                      /* thumbstick click, trackpad touch */
    r[0x0c] = 0x80;                      /* trigger */
    r[0x10] = 0x00;  r[0x11] = 0x40;     /* x 16384 */
    r[0x12] = 0x00;  r[0x13] = 0x80;     /* y -32768 */
    r[0x15] = 0x79;                      /* every touch */
    /* Every axis a value of its own, so a name on the wrong slot shows. */
    r[0x16] = 0xff;  r[0x17] = 0x00;  r[0x18] = 0x81;  r[0x19] = 0x40;
    r[0x1a] = 0x10;  r[0x1b] = 0xfe;

    s.connected = true;
    const bool ok = thhIndexFeed(r, sizeof(r), s);
    const string want =
        "a b system trigger_click thumbstick a_touch b_touch system_touch "
        "trigger_touch thumbstick_touch trackpad_touch | grip_top=1.000 "
        "grip_mid=0.000 grip_low=0.506 trigger_near=0.251 grip_force=0.063 "
        "trigger=0.502 pad_x=0.500 pad_y=-1.000 pad_force=0.996 "
        "stick_x=0.000 stick_y=0.000 tilt=0.000 roll=0.000 twist=0.000";

    report("a report built by hand",
           !ok ? "refused" : describe(s) == want ? "" :
           "read as\n    " + describe(s) + "\n  wanted\n    " + want);

    /* The same x and y with the trackpad let go are the stick's. */
    r[0x0a] = 0x00;
    s = thhState();
    thhIndexFeed(r, sizeof(r), s);
    report("x and y are the stick's when the trackpad isn't touched",
           s.axis[THH_INDEX_STICK_X] > 0.49f && s.axis[THH_INDEX_PAD_X] == 0 ?
           "" : describe(s));

    struct Bad
    {
        const char *name;
        size_t      len;
        uint8_t     id, type;
    };
    static const Bad BAD[] = {
        { "a report one byte too short", 0x1b, 0x01, 0x07 },
        { "another report ID",           64,   0x02, 0x07 },
        { "a ping",                      64,   0x01, 0x04 },
        { "nothing at all",              0,    0x01, 0x07 },
    };

    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        r[0x00] = BAD[i].id;
        r[0x02] = BAD[i].type;
        before = thhState();
        before.axis[0] = 0.25f;
        s = before;

        const bool took = thhIndexFeed(r, BAD[i].len, s);

        report(BAD[i].name, !took && describe(s) == describe(before) ? "" :
               took ? "taken as input" : "refused but changed the state");
    }
}

static void
checkDescriptors (const string &path)
{
    std::ifstream in(path.c_str());
    string line;
    int n = 0;

    while (std::getline(in, line))
    {
        const size_t tab = line.find('\t');
        const string name = line.substr(0, tab);
        const std::vector<uint8_t> d = unhex(line.substr(tab + 1));
        const bool input = thhIndexIsInput(d.data(), d.size());
        const bool imu = thhIndexIsImu(d.data(), d.size());

        n++;
        report("the " + name + " interface's descriptor",
               input != (name == "input") ?
                   (input ? "taken for the input's" : "not taken for the input's")
               : imu != (name == "imu") ?
                   (imu ? "taken for the IMU's" : "not taken for the IMU's")
               : "");

        /* Cut short anywhere, it is read no further than it goes; and cut
           before its Input item (0x81), not even the input's is. */
        const size_t input81 = std::find(d.begin(), d.end(), 0x81) - d.begin();

        for (size_t cut = 0; cut < d.size(); cut++)
            if ((name != "input" || cut <= input81 + 1) &&
                thhIndexIsInput(d.data(), cut))
                report("the " + name + " interface's descriptor cut at " +
                       std::to_string(cut), "taken for the input's");
    }

    report("the descriptors", n == 3 ? "" : "read " + std::to_string(n));

    /* The input's own shape with a byte less in its report is not it. */
    const uint8_t short63[] = { 0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15,
                                0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f,
                                0x09, 0x01, 0x81, 0x02, 0xc0 };

    report("a 63-byte input report with no IDs",
           thhIndexIsInput(short63, sizeof(short63)) ? "taken for the input's"
                                                     : "");
}

/* Each pose's reports through a filter of its own, so a still pose reads
   as itself rather than as the way there from the last one. */
static void
checkImu (const char *path)
{
    struct Pose
    {
        const char *label;
        float       tilt[2], roll[2];   /* where it settles: least, most */
        float       twist;              /* how far twist reaches each way */
    };
    static const Pose POSES[] = {
        { "hold it the way you play it, still", { 0.2f, 0.35f },
          { -0.15f, 0.15f }, 0 },
        { "tip the front (the stick end) UP, still", { 0.8f, 1 },
          { -1, 1 }, 0 },
        { "tip the front DOWN, still", { -0.5f, -0.3f }, { -0.2f, 0.2f }, 0 },
        { "roll your wrist so the buttons face LEFT, still", { -0.15f, 0.15f },
          { 0.8f, 1 }, 0 },
        { "roll your wrist so the buttons face RIGHT, still", { -0.15f, 0.15f },
          { -1, -0.9f }, 0 },
        { "twist it left and right a few times, like turning a key",
          { -1, 1 }, { -1, 1 }, 0.5f },
    };
    std::ifstream in(path);
    std::map<string, std::vector<std::vector<uint8_t> > > seen;
    string line;

    while (std::getline(in, line))
    {
        const size_t tab = line.find('\t');

        seen[line.substr(0, tab)].push_back(unhex(line.substr(tab + 1)));
    }

    for (size_t p = 0; p < sizeof(POSES) / sizeof(POSES[0]); p++)
    {
        const Pose &pose = POSES[p];
        thhIndexImu imu;
        thhState s;
        float lo = 0, hi = 0;
        size_t fed = 0;

        for (const std::vector<uint8_t> &r : seen[pose.label])
            if (thhIndexFeedImu(r.data(), r.size(), imu, s))
            {
                fed++;
                lo = std::min(lo, s.axis[THH_INDEX_TWIST]);
                hi = std::max(hi, s.axis[THH_INDEX_TWIST]);
            }

        const float tilt = s.axis[THH_INDEX_TILT], roll = s.axis[THH_INDEX_ROLL];
        char got[128];

        snprintf(got, sizeof(got), "tilt %.2f, roll %.2f, twist %.2f..%.2f "
                 "from %zu reports", tilt, roll, lo, hi, fed);
        report(pose.label,
               fed && tilt >= pose.tilt[0] && tilt <= pose.tilt[1] &&
               roll >= pose.roll[0] && roll <= pose.roll[1] &&
               (pose.twist ? hi >= pose.twist && lo <= -pose.twist
                           : std::max(hi, -lo) < 0.05f) ? "" : got);
    }

    /* A short report, or the input's, is refused without moving anything. */
    uint8_t r[64] = { 0x20 };
    thhIndexImu imu;
    thhState s;

    report("an IMU report too short, or the input's, is refused",
           !thhIndexFeedImu(r, 51, imu, s) &&
           !thhIndexFeedImu((r[0] = 0x01, r), 64, imu, s) &&
           s.axis[THH_INDEX_TILT] == 0 ? "" : "taken");
}

int
main (int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: hidcheck <scripts/hid/index-input.txt> "
                "<scripts/hid/index-descriptors.txt> "
                "<scripts/hid/index-imu.txt>\n");
        return 2;
    }

    const thhControls &c = thhIndexControls();

    report("the control table matches the decoder's slots",
           c.buttons.size() == THH_INDEX_BUTTONS &&
           c.axes.size() == THH_INDEX_AXES ? "" : "it doesn't");

    checkCapture(argv[1]);
    checkByHand();
    checkDescriptors(argv[2]);
    checkImu(argv[3]);

    printf("\n%d failure%s\n", failed, failed == 1 ? "" : "s");

    return failed;
}
