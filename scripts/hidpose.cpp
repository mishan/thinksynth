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
 * hidpose -- whether the Index controllers' decoded orientation is the
 * one they are really in.
 *
 *   hidpose          walks through poses that can be set up by eye, checks
 *                    what each controller reads in each, and prints a table
 *   hidpose live     says over and over, in words, what each one reads
 *
 * Each pose is read from the controller's own reports, decoded by
 * thhIndexFeedImu as the app decodes them, and alongside it the raw
 * accelerometer's direction and the gyro's turn about each of its axes,
 * unsprung, so a wrong axis or sign shows as such. Needs the controllers
 * plugged in and readable (hid/install-udev-rule.sh).
 */

#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "thhIndex.h"
#include "thhIndexHidraw.h"

using std::string;

struct Pad
{
    string       serial, hand;
    int          imu = -1;
    std::vector<int> fds;
    thhIndexImu  dec;
    thhState     s;

    /* Raw: gravity averaged since mark(), and degrees turned about each
       gyro axis since then. */
    double       g[3] = { 0, 0, 0 };
    int          n = 0;
    double       turned[3] = { 0, 0, 0 };
    bool         primed = false;
    uint8_t      seq = 0;
    uint32_t     tick = 0;

    void mark ()
    {
        memset(g, 0, sizeof(g));
        memset(turned, 0, sizeof(turned));
        n = 0;
    }
};

/* The fresh samples of a report 0x20, as thhIndexFeedImu takes them. */
static void
raw (Pad &p, const uint8_t *d, size_t len)
{
    if (len < 52 || d[0] != 0x20)
        return;
    for (int round = 0; round < 3; round++)
    {
        int best = -1;

        for (int k = 0; k < 3; k++)
        {
            const int8_t ahead = (int8_t)(d[1 + 17 * k + 16] - p.seq);

            if ((!p.primed || ahead > 0) &&
                (best < 0 || ahead < (int8_t)(d[1 + 17 * best + 16] - p.seq)))
                best = k;
        }
        if (best < 0)
            return;

        const uint8_t *s = d + 1 + 17 * best;
        const uint32_t tick = s[12] | (s[13] << 8) | (s[14] << 16) |
                              ((uint32_t)s[15] << 24);
        const double dt = (uint32_t)(tick - p.tick) / 48e6;

        for (int k = 0; k < 3; k++)
        {
            p.g[k] += (int16_t)(s[2 * k] | (s[2 * k + 1] << 8));
            if (p.primed && dt > 0 && dt < 0.1)
                p.turned[k] += (int16_t)(s[6 + 2 * k] | (s[7 + 2 * k] << 8)) /
                               16.4 * dt;
        }
        p.n++;
        p.seq = s[16];
        p.tick = tick;
        p.primed = true;
    }
}

/* Reads whatever has come, for up to ms; true if stdin has a line. */
static bool
pump (std::vector<Pad> &pads, int ms, bool wantLine)
{
    std::vector<pollfd> fds;

    for (const Pad &p : pads)
        fds.push_back({ p.imu, POLLIN, 0 });
    if (wantLine)
        fds.push_back({ 0, POLLIN, 0 });

    if (poll(fds.data(), fds.size(), ms) <= 0)
        return false;
    for (size_t i = 0; i < pads.size(); i++)
        if (fds[i].revents)
        {
            uint8_t buf[64];
            const ssize_t got = read(pads[i].imu, buf, sizeof(buf));

            /* Gone: the poll would say so at once, every time, and
               every reading after it would be the last one's. */
            if (got <= 0 && !(got < 0 && (errno == EAGAIN || errno == EINTR)))
            {
                fprintf(stderr, "\nthe %s controller is gone\n",
                        pads[i].hand.c_str());
                exit(1);
            }
            if (got > 0)
            {
                raw(pads[i], buf, (size_t)got);
                thhIndexFeedImu(buf, (size_t)got, pads[i].dec, pads[i].s);
            }
        }
    if (wantLine && fds.back().revents)
    {
        char line[256];

        if (fgets(line, sizeof(line), stdin) == NULL)
            exit(1);
        return true;
    }
    return false;
}

static void
waitFor (std::vector<Pad> &pads, double seconds)
{
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::duration<double>(seconds);

    while (std::chrono::steady_clock::now() < end)
        pump(pads, 10, false);
}

static double deg (double r) { return r * 180 / M_PI; }

/* The decoded axes, in degrees as a person would say them. */
static string
words (const Pad &p)
{
    char b[160];
    const double tilt = p.s.axis[THH_INDEX_TILT] * 90;
    const double roll = deg(std::asin(p.s.axis[THH_INDEX_ROLL]));

    snprintf(b, sizeof(b), "front %3.0f deg %s level, buttons %3.0f deg %s, "
             "turn %+.2f, twist %+.2f",
             std::fabs(tilt), tilt >= 0 ? "above" : "below", std::fabs(roll),
             roll >= 0 ? "left " : "right", p.s.axis[THH_INDEX_TURN],
             p.s.axis[THH_INDEX_TWIST]);
    return b;
}

static string
gravity (const Pad &p)
{
    char b[64];
    const double m = std::sqrt(p.g[0] * p.g[0] + p.g[1] * p.g[1] + p.g[2] * p.g[2]);

    if (!p.n || m == 0)
        return "no samples";
    snprintf(b, sizeof(b), "up is (%+.2f %+.2f %+.2f)", p.g[0] / m,
             p.g[1] / m, p.g[2] / m);
    return b;
}

/* A pose to set up, and the tilt and roll (degrees) it should read; a
   roll of NAN is any, where the front points straight up or down. */
struct Pose
{
    const char *say;
    double      tilt, roll;
};

static const Pose POSES[] = {
    { "front pointing straight up", 90, NAN },
    { "front level, buttons facing straight up", 0, 0 },
    { "front level, rolled so the buttons face your left", 0, 90 },
    { "front level, rolled so the buttons face your right", 0, -90 },
    { "front pointing straight down", -90, NAN },
    { "front tipped up about halfway, 45 degrees, buttons up", 45, 0 },
};

/* How far off a pose may read and still be it: what an eye sets up. */
#define TOLERANCE 15.0

static bool
ask (std::vector<Pad> &pads, const string &what)
{
    printf("\n%s\n  then press Enter ", what.c_str());
    fflush(stdout);
    while (!pump(pads, 50, true))
        ;
    return true;
}

static void
settle (std::vector<Pad> &pads)
{
    waitFor(pads, 1.0);
    for (Pad &p : pads)
        p.mark();
    waitFor(pads, 1.0);
}

static bool
check (Pad &p, const Pose &pose, string &row)
{
    const double tilt = p.s.axis[THH_INDEX_TILT] * 90;
    const double roll = deg(std::asin(p.s.axis[THH_INDEX_ROLL]));
    const bool ok = std::fabs(tilt - pose.tilt) <= TOLERANCE &&
                    (std::isnan(pose.roll) || std::fabs(roll - pose.roll) <= TOLERANCE);
    char b[256];

    snprintf(b, sizeof(b), "%-5s %-55s want tilt %+4.0f roll %4s  got tilt %+4.0f "
             "roll %+4.0f  %s  %s", p.hand.c_str(), pose.say, pose.tilt,
             std::isnan(pose.roll) ? "any" : std::to_string((int)pose.roll).c_str(),
             tilt, roll, gravity(p).c_str(), ok ? "ok" : "OFF");
    row = b;
    return ok;
}

int
main (int argc, char **argv)
{
    const bool live = argc > 1 && string(argv[1]) == "live";
    std::vector<Pad> pads;

    setvbuf(stdout, NULL, _IOLBF, 0);
    for (const thhIndexNodes &n : thhFindIndexes("/sys", "/dev"))
    {
        Pad p;
        thhConnection c;
        string status;

        p.serial = n.serial;
        /* read is the input's fd, then the IMU's if it has one. */
        if (!thhConnectIndex("/sys", "/dev", n.usb, c, status) ||
            c.read.size() < 2)
        {
            fprintf(stderr, "%s: %s\n", n.serial.c_str(),
                    status.empty() ? "not both an input and an IMU" :
                    status.c_str());
            for (int fd : c.fds)
                close(fd);
            continue;
        }
        p.imu = c.read[1];
        p.fds = c.fds;
        p.hand = c.hand;
        if (p.hand.empty())
            p.hand = "?" + n.serial.substr(n.serial.size() > 4 ? n.serial.size() - 4 : 0);
        pads.push_back(p);
    }
    /* The page and the prompts tell controllers apart by hand. */
    for (size_t i = 1; i < pads.size(); i++)
        if (pads[i].hand == pads[0].hand)
            pads[i].hand += "-" + pads[i].serial.substr(
                pads[i].serial.size() > 4 ? pads[i].serial.size() - 4 : 0);
    if (pads.empty())
    {
        fprintf(stderr, "no Valve Index controller to read\n");
        return 1;
    }
    for (const Pad &p : pads)
        printf("%s is %s\n", p.hand.c_str(), p.serial.c_str());

    if (live)
    {
        for (;;)
        {
            waitFor(pads, 0.2);
            for (Pad &p : pads)
            {
                printf("%-5s %s  %s\n", p.hand.c_str(), words(p).c_str(),
                       gravity(p).c_str());
                p.mark();
            }
            printf("\n");
        }
    }

    std::vector<string> rows;
    int off = 0;

    /* Each controller's reading is its own, not the other's. */
    if (pads.size() == 2)
    {
        for (int up = 0; up < 2; up++)
        {
            ask(pads, "Stand the " + pads[up].hand + " controller with its front "
                "pointing straight up, and lay the " + pads[1 - up].hand +
                " one front level, buttons facing up.");
            settle(pads);
            for (int i = 0; i < 2; i++)
            {
                string row;

                off += !check(pads[i], POSES[i == up ? 0 : 1], row);
                rows.push_back(row);
            }
        }
    }

    for (Pad &p : pads)
        for (const Pose &pose : POSES)
        {
            string row;

            ask(pads, "Hold the " + p.hand + " controller still with its " +
                pose.say + ".");
            settle(pads);
            off += !check(p, pose, row);
            rows.push_back(row);
        }

    /* Turn: about the front, which is what twist and turn measure. */
    for (Pad &p : pads)
    {
        char b[256];

        ask(pads, "Stand the " + p.hand + " controller front straight up. When "
            "you press Enter, turn it a quarter turn to your left (anticlockwise "
            "seen from above) over about a second, and hold it there");
        waitFor(pads, 0.2);
        p.mark();
        waitFor(pads, 2.5);
        /* Anticlockwise about up, which is the front, -x: negative x. */
        const bool ok = std::fabs(p.turned[0] + 90) <= 2 * TOLERANCE &&
                        std::fabs(p.turned[1]) <= 2 * TOLERANCE &&
                        std::fabs(p.turned[2]) <= 2 * TOLERANCE;

        off += !ok;
        snprintf(b, sizeof(b), "%-5s quarter turn left, front up: turned about "
                 "x %+4.0f  y %+4.0f  z %+4.0f deg (want x about -90, y and z "
                 "near 0); turn reads %+.2f  %s", p.hand.c_str(), p.turned[0],
                 p.turned[1], p.turned[2], p.s.axis[THH_INDEX_TURN],
                 ok ? "ok" : "OFF");
        rows.push_back(b);
        printf("\n%s\n", b);
    }

    printf("\n");
    for (const string &r : rows)
        printf("%s\n", r.c_str());
    printf("\n%d pose%s off\n", off, off == 1 ? "" : "s");
    return off ? 1 : 0;
}
