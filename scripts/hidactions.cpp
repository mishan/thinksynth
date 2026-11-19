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
 * hidactions -- what the session asks to play for the captures in
 * scripts/hid, one line of JSON (thhHost::take) for each report that
 * asked for any. wasm/web/hidcheck.mjs runs the browser's build of the
 * same code over the same captures and holds it to these lines.
 *
 *   hidactions WIRELESS INPUT IMU
 *
 * WIRELESS is a dongle's capture, played as the right hand of a pair, and
 * then the dongle saying its controller has gone; INPUT and IMU are a
 * wired controller's two interfaces', the left hand and then alone. 4 ms
 * a report.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "thhHost.h"

static std::vector<std::vector<uint8_t> >
reports (const char *path)
{
    std::vector<std::vector<uint8_t> > out;
    std::ifstream in(path);
    std::string line;

    while (std::getline(in, line))
    {
        const std::string hex = line.substr(line.find('\t') + 1);
        std::vector<uint8_t> r;

        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            r.push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), NULL, 16));
        out.push_back(r);
    }
    return out;
}

int
main (int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: hidactions WIRELESS INPUT IMU\n");
        return 2;
    }

    thhHost host;
    double t = 1;
    const auto play = [&](int id, thhHost::From from, const char *path) {
        for (const std::vector<uint8_t> &r : reports(path))
        {
            host.report(id, from, r.data(), r.size(), t += 0.004);
            host.tick(t);

            const std::string acts = host.take();

            if (acts != "[]")
                printf("%s\n", acts.c_str());
        }
    };

    const int dongle = host.add("right", "LHR-R", t);
    const int wired = host.add("", "", t);
    const uint8_t gone[] = { 0x26, 0x01 };

    play(dongle, thhHost::RADIO, argv[1]);
    if (host.report(dongle, thhHost::RADIO, gone, sizeof(gone), t += 0.004))
        host.drop(dongle, t);
    printf("%s\n", host.take().c_str());

    play(wired, thhHost::INPUT, argv[2]);
    play(wired, thhHost::IMU, argv[3]);
    host.release(t += 0.004);
    printf("%s\n", host.take().c_str());
    return 0;
}
