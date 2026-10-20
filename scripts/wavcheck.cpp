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
 */

/*
 * wavcheck -- the two wav readers, on files nobody shipped.
 *
 *     scripts/wavcheck [-n ITERATIONS] DIR
 *
 * osc::sample's (plugins/osc/sampleslot.h) reads what somebody drops in
 * for an instrument; thsound::readWav (libthink/thSoundFile.cpp) the
 * target an evolution or an audition listens for. The shipped samples are
 * all one format, so nothing else had ever been read. Here:
 *
 *   every format each reader says it reads -- PCM of each width, float --
 *   mono and stereo, with a plain, an 18-byte and an extensible `fmt ',
 *   and an odd-length chunk before the data: the ramp written comes back
 *   to within a step of its width;
 *   a format it does not read, a file with its data before its format, no
 *   channels, a data chunk longer than the file, a chunk that claims four
 *   gigabytes: refused or cut short, never read past;
 *   a float file holding a NaN or an infinity: finite samples, since one
 *   non-finite sample costs the voice playing it;
 *   then ITERATIONS files made by changing a few bytes of good ones, through
 *   both readers: whatever comes back is finite and no longer than the
 *   reader allows. Under the sanitizers (the asan job) that is also where
 *   a read past the end shows.
 *
 * DIR is somewhere to write files, since one reader takes only a path.
 * Exit status is the number of failures.
 */

#include "config.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "think.h"
#include "thSoundFile.h"
#include "../plugins/osc/sampleslot.h"

static int failures = 0;

static void
fail (const std::string &what)
{
    printf("FAIL  %s\n", what.c_str());
    failures++;
}

static const unsigned RATE = 48000;

/* A ramp from -0.75 to 0.75: every value distinct, none at full scale,
   where one width's top is another's overflow. */
static std::vector<double>
ramp (size_t n)
{
    std::vector<double> v(n);

    for (size_t i = 0; i < n; i++)
        v[i] = -0.75 + 1.5 * (double)i / (double)(n - 1);

    return v;
}

struct Wav
{
    unsigned format = 1;        /* 1 PCM, 3 float                       */
    unsigned channels = 1;
    unsigned bits = 16;
    unsigned long rate = RATE;
    unsigned fmtLen = 16;       /* 16, 18, or 40 for extensible         */
    bool oddChunk = false;      /* a 3-byte chunk, and its pad, first   */
    bool dataFirst = false;
    long dataLen = -1;          /* the data chunk's length field; -1:
                                   what it holds                        */
    std::vector<double> samples;
};

static void
put16 (std::vector<unsigned char> &b, unsigned v)
{
    b.push_back(v & 0xff);
    b.push_back((v >> 8) & 0xff);
}

static void
put32 (std::vector<unsigned char> &b, unsigned long v)
{
    for (int i = 0; i < 4; i++)
        b.push_back((v >> (8 * i)) & 0xff);
}

static std::vector<unsigned char>
build (const Wav &w)
{
    std::vector<unsigned char> fmt, data, b;
    const unsigned bytes = w.bits / 8;

    put16(fmt, w.fmtLen == 40 ? 0xfffe : w.format);
    put16(fmt, w.channels);
    put32(fmt, w.rate);
    put32(fmt, w.rate * w.channels * bytes);
    put16(fmt, w.channels * bytes);
    put16(fmt, w.bits);

    if (w.fmtLen >= 18)
        put16(fmt, w.fmtLen - 18);

    if (w.fmtLen == 40)
    {
        put16(fmt, w.bits);
        put32(fmt, w.channels == 2 ? 3 : 4);
        put16(fmt, w.format);       /* the GUID's first two bytes */

        static const unsigned char rest[14] = {
            0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
            0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };

        fmt.insert(fmt.end(), rest, rest + 14);
    }

    for (double s : w.samples)
        for (unsigned c = 0; c < w.channels; c++)
        {
            if (w.format == 3)
            {
                const float f = (float)s;
                uint32_t u;

                memcpy(&u, &f, 4);
                put32(data, u);
                continue;
            }

            const double top = ldexp(1.0, (int)w.bits - 1);
            long long v = llround(s * top);

            if (v > (long long)top - 1)
                v = (long long)top - 1;

            if (w.bits == 8)
                v += 128;

            for (unsigned i = 0; i < bytes; i++)
                data.push_back((unsigned char)((unsigned long long)v >>
                                               (8 * i)));
        }

    const auto chunk = [&b] (const char *id, unsigned long len,
                             const std::vector<unsigned char> &body)
    {
        b.insert(b.end(), id, id + 4);
        put32(b, len);
        b.insert(b.end(), body.begin(), body.end());

        if (body.size() & 1)
            b.push_back(0);
    };

    b.insert(b.end(), { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' });

    if (w.oddChunk)
        chunk("junk", 3, { 1, 2, 3 });

    const unsigned long dl = w.dataLen < 0 ? data.size()
                                           : (unsigned long)w.dataLen;

    if (w.dataFirst)
        chunk("data", dl, data);

    chunk("fmt ", fmt.size(), fmt);

    if (!w.dataFirst)
        chunk("data", dl, data);

    const unsigned long riff = b.size() - 8;

    for (int i = 0; i < 4; i++)
        b[4 + i] = (riff >> (8 * i)) & 0xff;

    return b;
}

/* Each reader, the same question: the samples, or why not. */
struct Read
{
    bool ok;
    std::vector<float> mono;
    std::string why;
};

static std::string scratch;

static Read
bySampler (const std::vector<unsigned char> &bytes, bool viaFile)
{
    thSampleData out;
    Read r;

    if (viaFile)
    {
        const std::string path = scratch + "/sampler.wav";
        std::ofstream(path, std::ios::binary)
            .write((const char *)bytes.data(), (long)bytes.size());

        r.ok = thWavRead(path, RATE, out, r.why);
    }
    else
        r.ok = thWavReadMemory(bytes.data(), bytes.size(), RATE, out, r.why);

    r.mono = out.frames;

    return r;
}

static Read
byTarget (const std::vector<unsigned char> &bytes)
{
    const std::string path = scratch + "/target.wav";
    Read r;

    std::ofstream(path, std::ios::binary)
        .write((const char *)bytes.data(), (long)bytes.size());
    r.ok = thsound::readWav(path, r.mono, r.why, RATE);

    return r;
}

static bool
allFinite (const std::vector<float> &v)
{
    for (float x : v)
        if (!std::isfinite(x))
            return false;

    return true;
}

/* Back within a step of the width written, and as many as written. */
static std::string
compare (const Read &r, const Wav &w)
{
    if (!r.ok)
        return "refused: " + r.why;

    if (r.mono.size() != w.samples.size())
        return std::to_string(r.mono.size()) + " frames, not " +
               std::to_string(w.samples.size());

    /* A float's own precision too, since that is what comes back. */
    const double step = w.format == 3
        ? 1e-6 : std::max(2 * ldexp(1.0, 1 - (int)w.bits), 1e-6);

    for (size_t i = 0; i < r.mono.size(); i++)
        if (fabs(r.mono[i] - w.samples[i]) > step)
            return "frame " + std::to_string(i) + " is " +
                   std::to_string(r.mono[i]) + ", not " +
                   std::to_string(w.samples[i]);

    return "";
}

static std::string
describe (const Wav &w)
{
    return std::string(w.format == 3 ? "float" : "PCM") + " " +
           std::to_string(w.bits) + "-bit, " +
           (w.channels == 2 ? "stereo" : "mono") + ", fmt " +
           std::to_string(w.fmtLen) + (w.oddChunk ? ", odd chunk first" : "");
}

static void
formats (void)
{
    struct Format { unsigned format, bits; bool sampler, target; };
    static const Format all[] = {
        { 1, 8,  true,  false },
        { 1, 16, true,  true  },
        { 1, 24, true,  true  },
        { 1, 32, true,  true  },
        { 3, 32, true,  true  },
    };
    int checked = 0;
    const int before = failures;

    for (const Format &f : all)
        for (unsigned channels : { 1u, 2u })
            for (unsigned fmtLen : { 16u, 18u, 40u })
                for (bool odd : { false, true })
                {
                    Wav w;

                    w.format = f.format;
                    w.bits = f.bits;
                    w.channels = channels;
                    w.fmtLen = fmtLen;
                    w.oddChunk = odd;
                    w.samples = ramp(301);

                    const std::vector<unsigned char> b = build(w);
                    const struct { const char *who; bool reads; Read r; }
                        readers[] = {
                            { "osc::sample, from memory", f.sampler,
                              bySampler(b, false) },
                            { "osc::sample, from a file", f.sampler,
                              bySampler(b, true) },
                            { "thsound::readWav", f.target, byTarget(b) },
                        };

                    for (const auto &rd : readers)
                    {
                        const std::string bad = compare(rd.r, w);

                        if (rd.reads && !bad.empty())
                            fail(std::string(rd.who) + ", " + describe(w) +
                                 ": " + bad);
                        else if (!rd.reads && rd.r.ok)
                            fail(std::string(rd.who) + ", " + describe(w) +
                                 ": read a format it says it does not");

                        checked++;
                    }
                }

    if (failures == before)
        printf("ok    %d reads of every format, layout and channel count\n",
               checked);
}

static void
malformed (void)
{
    const auto good = [] {
        Wav w;

        w.samples = ramp(100);
        return w;
    };

    struct Case { const char *what; Wav w; bool readable; };
    std::vector<Case> cases;
    const int before = failures;

    {
        Wav w = good();
        w.dataFirst = true;
        cases.push_back({ "its data before its format", w, false });
    }
    {
        Wav w = good();
        w.channels = 0;
        cases.push_back({ "no channels", w, false });
    }
    {
        Wav w = good();
        w.dataLen = 100000;
        cases.push_back({ "a data chunk longer than the file", w, true });
    }
    {
        Wav w = good();
        w.bits = 12;
        cases.push_back({ "12-bit PCM", w, false });
    }

    for (const Case &c : cases)
    {
        const std::vector<unsigned char> b = build(c.w);
        const Read reads[] = { bySampler(b, false), bySampler(b, true),
                               byTarget(b) };

        for (const Read &r : reads)
            if (r.ok && (!c.readable || r.mono.size() > c.w.samples.size() ||
                         !allFinite(r.mono)))
                fail(std::string(c.what) + ": read as " +
                     std::to_string(r.mono.size()) + " frames");
    }

    /* A chunk before the data claiming most of four gigabytes: past the
       end of the file, so nothing is read, and on a host whose long is 32
       bits not a step backwards into the chunk just read, for ever. */
    {
        Wav w = good();
        std::vector<unsigned char> b = build(w);
        const unsigned char huge[8] = { 'j', 'u', 'n', 'k',
                                        0xf8, 0xff, 0xff, 0xff };

        b.insert(b.begin() + 12, huge, huge + 8);

        const Read reads[] = { bySampler(b, false), bySampler(b, true),
                               byTarget(b) };

        for (const Read &r : reads)
            if (r.ok)
                fail("a chunk claiming 4 GB was read past: " +
                     std::to_string(r.mono.size()) + " frames");
    }

    if (failures == before)
        printf("ok    malformed files refused or cut short, never read "
               "past\n");

    /* Floats that are not numbers. */
    {
        Wav w = good();

        w.format = 3;
        w.bits = 32;
        w.samples[10] = NAN;
        w.samples[20] = INFINITY;
        w.samples[30] = -INFINITY;

        const std::vector<unsigned char> b = build(w);
        const struct { const char *who; Read r; } reads[] = {
            { "osc::sample", bySampler(b, false) },
            { "thsound::readWav", byTarget(b) },
        };

        for (const auto &rd : reads)
            if (!rd.r.ok || !allFinite(rd.r.mono))
                fail(std::string(rd.who) + ": a float file with a NaN and "
                     "infinities read as non-finite samples");
            else
                printf("ok    %s reads a NaN and infinities as finite\n",
                       rd.who);
    }
}

/* The good files with a few bytes changed, or cut short: whatever comes
   back is finite and within the reader's own limit. */
static void
mutations (int iterations)
{
    std::vector<std::vector<unsigned char> > seeds;

    for (unsigned bits : { 8u, 16u, 24u, 32u })
        for (unsigned fmtLen : { 16u, 40u })
        {
            Wav w;

            w.bits = bits;
            w.fmtLen = fmtLen;
            w.channels = 2;
            w.oddChunk = true;
            w.samples = ramp(64);
            seeds.push_back(build(w));
        }

    {
        Wav w;

        w.format = 3;
        w.bits = 32;
        w.samples = ramp(64);
        seeds.push_back(build(w));
    }

    uint64_t state = 0x9e3779b97f4a7c15ull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    int bad = 0;

    for (int i = 0; i < iterations && bad < 10; i++)
    {
        std::vector<unsigned char> b = seeds[next() % seeds.size()];
        const int edits = 1 + (int)(next() % 8);

        for (int e = 0; e < edits; e++)
        {
            /* Mostly the header, where the lengths and the format are. */
            const size_t at = next() % 3 ? next() % 60 : next() % b.size();

            if (at < b.size())
                b[at] = (unsigned char)next();
        }

        if (next() % 4 == 0)
            b.resize(next() % b.size());

        const Read reads[] = { bySampler(b, false), byTarget(b) };

        for (const Read &r : reads)
            if (r.ok && (!allFinite(r.mono) ||
                         r.mono.size() > THINK_SAMPLE_FRAMES_MAX))
            {
                fail("mutation " + std::to_string(i) + ": " +
                     std::to_string(r.mono.size()) + " frames, " +
                     (allFinite(r.mono) ? "finite" : "not all finite"));
                bad++;
            }
    }

    if (bad == 0)
        printf("ok    %d files of changed bytes through both readers\n",
               iterations);
}

int
main (int argc, char *argv[])
{
    int iterations = 20000;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            iterations = atoi(argv[++i]);
        else
            scratch = argv[i];
    }

    if (scratch.empty())
    {
        fprintf(stderr, "usage: wavcheck [-n ITERATIONS] DIR\n");
        return 2;
    }

    std::error_code ec;

    std::filesystem::create_directories(scratch, ec);

    formats();
    malformed();
    mutations(iterations);

    if (failures == 0)
        printf("wavcheck: OK\n");
    else
        printf("wavcheck: %d failure%s\n", failures,
               failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
