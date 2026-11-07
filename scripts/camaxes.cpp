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
 * camaxes -- what thMotion makes of a real camera, live, as text.
 *
 *   camaxes [-d /dev/videoN] [-s SECONDS]
 *
 * Ten lines a second: the frame rate, motion as a bar, the place, and the
 * nine zones as the player sees them. For tuning thMotion's constants
 * against a hand in a room; not a test, since it needs a camera.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "thiCamera.h"
#include "thiV4l2.h"

/* Ctrl-C ends the run the way the clock does, so the camera's exposure
   setting is put back as it closes. */
static volatile sig_atomic_t interrupted = 0;

static void interrupt (int)
{
    interrupted = 1;
}

int main (int argc, char **argv)
{
    std::string device;
    double seconds = 30;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)
            device = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else
        {
            fprintf(stderr, "usage: %s [-d /dev/videoN] [-s SECONDS]\n", argv[0]);
            return 2;
        }
    }

    signal(SIGINT, interrupt);

    thiCamera cam([&] (std::string &status) { return thiOpenV4l2(device, status); },
                  [] { });
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    std::vector<thiAxes> frames;

    while (!interrupted && std::chrono::steady_clock::now() < end)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!cam.drain(frames))
        {
            printf("%s\n", cam.status().empty() ? "(no frames)" : cam.status().c_str());
            continue;
        }

        /* The loudest of the frames since the last line, so a strike shorter
           than a line still shows. */
        thiAxes peak = frames.back();

        for (const auto &f : frames)
            for (int a = thMotion::MOTION; a < thMotion::AXES; a++)
                if (a == thMotion::MOTION || a >= thMotion::ZONE1)
                    peak.axis[a] = std::max(peak.axis[a], f.axis[a]);

        const double span = frames.back().seconds - frames.front().seconds;
        const double fps = frames.size() > 1 && span > 0 ? (frames.size() - 1) / span : 0;
        const int bar = (int)(peak.axis[thMotion::MOTION] * 20 + 0.5);
        char zones[3][16];

        for (int row = 0; row < 3; row++)
            snprintf(zones[row], sizeof zones[row], "%3.0f%3.0f%3.0f",
                     99 * peak.axis[thMotion::ZONE1 + 3 * row],
                     99 * peak.axis[thMotion::ZONE2 + 3 * row],
                     99 * peak.axis[thMotion::ZONE3 + 3 * row]);

        printf("%5.1f fps  motion %.2f %-20.*s  x %.2f y %.2f spread %.2f bright %.2f  |%s|%s|%s|\n",
               fps, peak.axis[thMotion::MOTION], bar, "####################",
               peak.axis[thMotion::X], peak.axis[thMotion::Y], peak.axis[thMotion::SPREAD],
               peak.axis[thMotion::BRIGHT], zones[0], zones[1], zones[2]);
        fflush(stdout);
    }

    return 0;
}
