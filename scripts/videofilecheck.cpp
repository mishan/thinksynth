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
 * videofilecheck -- thVideoFile, a video synth's frames as a .y4m.
 *
 * Writes two frames of one row of pixels and reads the file back: its
 * header, as a reader parses it, and each pixel's Y, Cb and Cr against
 * BT.601's limited-range values for that color.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "thUtil.h"
#include "thVideoFile.h"

using std::string;

static int failed = 0;

static void
report (const string &name, const string &got, const string &want)
{
    if (got == want)
        printf("ok    %s: %s\n", name.c_str(), got.c_str());
    else
    {
        printf("FAIL  %s\n  got  %s\n  want %s\n", name.c_str(), got.c_str(), want.c_str());
        failed++;
    }
}

int
main (void)
{
    static const struct { const char *name; float r, g, b; const char *ycbcr; } colors[] = {
        { "black", 0, 0, 0, "16 128 128" },
        { "white", 1, 1, 1, "235 128 128" },
        { "red", 1, 0, 0, "81 90 240" },
        { "green", 0, 1, 0, "145 54 34" },
        { "blue", 0, 0, 1, "41 240 110" },
        { "past white", 3, 2, 1.5f, "235 128 128" },
        { "under black", -1, -0.5f, -2, "16 128 128" },
        { "not a number", NAN, NAN, NAN, "16 128 128" },
        { "infinite", INFINITY, INFINITY, INFINITY, "235 128 128" },
    };
    const int W = sizeof(colors) / sizeof(colors[0]), H = 1;
    std::vector<float> r, g, b;

    for (const auto &c : colors)
    {
        r.push_back(c.r);
        g.push_back(c.g);
        b.push_back(c.b);
    }

    const string path = thUtil::tempFile("videofilecheck-");
    string why;

    {
        thVideoFile file;

        if (!file.open(path, W, H, 60, why) || !file.write(r.data(), g.data(), b.data(), why) ||
            !file.write(r.data(), g.data(), b.data(), why) || !file.close(why))
        {
            report("the file is written", why, "");
            return 1;
        }
    }

    string bytes;

    /* Closed before the remove, which Windows refuses on an open file. */
    {
        std::ifstream in(path, std::ios::binary);

        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(path);

    /* The header, a token at a time: what is there, whatever the order. */
    const size_t eol = bytes.find('\n');
    std::istringstream header(bytes.substr(0, eol));
    string tok, magic, size, rate, scan, aspect, chroma;

    header >> magic;
    while (header >> tok)
        if (tok[0] == 'W' || tok[0] == 'H')
            size += tok;
        else if (tok[0] == 'F')
            rate = tok;
        else if (tok[0] == 'I')
            scan = tok;
        else if (tok[0] == 'A')
            aspect = tok;
        else if (tok[0] == 'C')
            chroma = tok;
    report("the header", magic + " " + size + " " + rate + " " + scan + " " + aspect + " " + chroma,
           "YUV4MPEG2 W" + std::to_string(W) + "H1 F60:1 Ip A1:1 C444");

    /* Two frames, each FRAME and its three planes. */
    const size_t frame = 6 + 3 * (size_t)W * H;
    const bool sized = eol != string::npos && bytes.size() == eol + 1 + 2 * frame &&
                       bytes.compare(eol + 1, 6, "FRAME\n") == 0 &&
                       bytes.compare(eol + 1 + frame, 6, "FRAME\n") == 0;

    report("two frames", sized ? "two" : std::to_string(bytes.size()) + " bytes", "two");
    if (!sized)
        return 1;

    for (int x = 0; x < W; x++)
    {
        const unsigned char *p = (const unsigned char *)bytes.data() + eol + 1 + frame + 6;
        char got[32];

        snprintf(got, sizeof(got), "%d %d %d", p[x], p[W * H + x], p[2 * W * H + x]);
        report(colors[x].name, got, colors[x].ycbcr);
    }

    return failed ? 1 : 0;
}
