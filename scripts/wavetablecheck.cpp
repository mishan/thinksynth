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
 * wavetablecheck -- does a wavetable come out band-limited, and is it read
 * the way its file says?
 *
 * plugins/osc/wavetable.h, against spectra this measures itself with a
 * plain DFT in double: each level holds exactly the harmonics it says,
 * at the amplitudes the frame has, the top one included and none above;
 * a cycle that is not a power of two long comes out as one that is; the
 * storage is what the formula says. The reader, on wavs written here
 * with each wavetable chunk and with none. And the 4-point read,
 * stepped at a note's pitch: how far under the harmonics its images are.
 *
 *   wavetablecheck SCRATCHDIR
 */

#include "config.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "think.h"
#include "../plugins/osc/sampleslot.h"
#include "../plugins/osc/wavetable.h"

/* How far under a note's fundamental the read's images have to stay,
   in dB, negative. */
#define THINK_WT_IMAGES_DB -80

static int failures = 0;

static void check (bool ok, const std::string &what)
{
    if (!ok)
    {
        fprintf(stderr, "FAIL %s\n", what.c_str());
        failures++;
    }
}

static std::string num (double v)
{
    char b[32];

    snprintf(b, sizeof b, "%.6g", v);
    return b;
}

/* The amplitude of harmonic h of a cycle of `n' samples: |X_h| 2 / n. */
static double harmonic (const float *x, size_t n, unsigned h)
{
    double a = 0, b = 0;

    for (size_t t = 0; t < n; t++)
    {
        const double angle = 2 * M_PI * (double)((h * t) % n) / (double)n;

        a += x[t] * cos(angle);
        b += x[t] * sin(angle);
    }

    return 2 * sqrt(a * a + b * b) / (double)n;
}

/* A saw of `harms' harmonics, 1/h each, `n' samples a cycle. */
static std::vector<float> saw (size_t n, unsigned harms)
{
    std::vector<float> x(n, 0);

    for (unsigned h = 1; h <= harms; h++)
        for (size_t t = 0; t < n; t++)
            x[t] += (float)(sin(2 * M_PI * (double)((h * t) % n) / n) / h);

    return x;
}

/* Each level holds the harmonics it says at the frame's amplitudes, its
   top one included, and nothing above. */
static void levels (void)
{
    const size_t n = 2048;
    std::vector<float> x(n);
    const unsigned harms = 1023;
    std::vector<float> s = saw(n, harms);

    for (size_t t = 0; t < n; t++)
        x[t] = (float)sin(2 * M_PI * (double)t / n);

    x.insert(x.end(), s.begin(), s.end());

    thWaveTable table;
    std::string why;

    if (!thWaveTableBuild(x.data(), x.size(), 2048, table, why))
    {
        check(false, "a sine and a saw build: " + why);
        return;
    }

    check(table.frames == 2 && table.size == 2048 &&
          table.levels.size() > 10 && table.levels[0].harmonics == 511,
          "two frames of 2048, harmonics to 511 at level 0, and levels "
          "down to the fundamental: " + std::to_string(table.levels.size()));

    for (size_t k = 0; k < table.levels.size(); k++)
    {
        const thWaveLevel &l = table.levels[k];
        const float *sine = l.data.data();
        const float *sw = l.data.data() + l.len;
        double worstIn = 0, worstAbove = 0;
        const unsigned probe[] = { 1, 2, 3, l.harmonics / 2 + 1, l.harmonics };

        for (unsigned h : probe)
            if (h >= 1 && h <= l.harmonics)
                worstIn = fmax(worstIn, fabs(harmonic(sw, l.len, h) -
                                             1.0 / h) * h);

        for (unsigned h = l.harmonics + 1; h < l.len / 2; h += 1 + h / 64)
            worstAbove = fmax(worstAbove, harmonic(sw, l.len, h));

        check(fabs(harmonic(sine, l.len, 1) - 1) < 1e-5 &&
              worstIn < 1e-4 && worstAbove < 1e-6,
              "level " + std::to_string(k) + " (" +
              std::to_string(l.harmonics) + " harmonics, " +
              std::to_string(l.len) + " samples): the sine whole, the "
              "saw's harmonics to the top at 1/h (off by " + num(worstIn) +
              "), none above (" + num(worstAbove) + ")");
    }

    /* The storage the formula gives: H_k = floor(511 / sqrt(2)^k), each
       kept at the power of two >= 8 H_k, 64 at the least. */
    size_t want = 0, got = 0;

    for (unsigned k = 0;; k++)
    {
        const unsigned h = (unsigned)floor(511 / pow(sqrt(2.0), k) + 1e-9);

        if (h < 1)
            break;

        size_t len = 64;

        while (len < 8 * h)
            len <<= 1;

        want += 2 * len;
    }

    for (const thWaveLevel &l : table.levels)
        got += l.data.size();

    check(got == want, "the levels hold " + std::to_string(got) +
                       " samples, the formula's " + std::to_string(want));
}

/* A frame whose harmonics all fit comes back as itself, wherever it is
   read, its phase and its DC and all: a level is the frame, not only its
   magnitudes.
   A power of two long and not. */
static void samePhase (void)
{
    for (size_t n : { (size_t)2048, (size_t)600 })
    {
        std::vector<float> x = saw(n, 40);

        /* Shifted, so a frame read backwards would not match, and off
           centre, so one that lost its DC would not. */
        std::rotate(x.begin(), x.begin() + n / 7, x.end());

        for (float &v : x)
            v += 0.25f;

        thWaveTable table;
        std::string why;
        double worst = 0;

        if (!thWaveTableBuild(x.data(), x.size(), 0, table, why))
        {
            check(false, "a frame builds: " + why);
            continue;
        }

        for (size_t t = 0; t < n; t++)
            worst = fmax(worst, fabs(thWaveRead(table.levels[0], 0,
                                                (double)t / n) - x[t]));

        check(worst < 2e-3,
              "a frame of " + std::to_string(n) + " whose harmonics all "
              "fit reads back as itself: off by " + num(worst));
    }
}

/* A cycle that is not a power of two long: the same harmonics. */
static void oddLength (void)
{
    std::vector<float> x = saw(600, 40);
    thWaveTable table;
    std::string why;

    if (!thWaveTableBuild(x.data(), x.size(), 0, table, why))
    {
        check(false, "a 600-sample cycle builds: " + why);
        return;
    }

    const thWaveLevel &l = table.levels[0];
    double worst = 0;

    for (unsigned h = 1; h <= 60; h++)
        worst = fmax(worst, fabs(harmonic(l.data.data(), l.len, h) -
                                 (h <= 40 ? 1.0 / h : 0)));

    check(table.size == 600 && table.frames == 1 && l.harmonics == 299 &&
          worst < 1e-5,
          "a 600-sample cycle is one frame, its harmonics where they were: "
          "off by " + num(worst));
}

/* A wav written here: 16-bit mono, `chunk' (id and bytes) before its
   data. */
static bool writeWav (const std::string &path, const std::vector<float> &x,
                      unsigned rate, const char *id, const std::string &body)
{
    FILE *f = fopen(path.c_str(), "wb");

    if (f == NULL)
        return false;

    auto u32 = [f] (unsigned long v)
    {
        const unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8),
                                     (unsigned char)(v >> 16),
                                     (unsigned char)(v >> 24) };
        fwrite(b, 1, 4, f);
    };
    auto u16 = [f] (unsigned v)
    {
        const unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
        fwrite(b, 1, 2, f);
    };
    const unsigned long extra = id ? 8 + body.size() + (body.size() & 1) : 0;

    fwrite("RIFF", 1, 4, f);
    u32(36 + extra + x.size() * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);

    if (id)
    {
        fwrite(id, 1, 4, f);
        u32(body.size());
        fwrite(body.data(), 1, body.size(), f);

        if (body.size() & 1)
            fputc(0, f);
    }

    fwrite("data", 1, 4, f);
    u32(x.size() * 2);

    for (float v : x)
        u16((unsigned)(int)lrint(v * 32767.0) & 0xffff);

    return fclose(f) == 0;
}

static std::string word32 (unsigned v)
{
    return std::string(1, (char)(v & 0xff)) + (char)((v >> 8) & 0xff) +
           (char)((v >> 16) & 0xff) + (char)((v >> 24) & 0xff);
}

/* The reader: which chunks say how long a frame is, and the table each
   file makes. */
static void reader (const std::string &dir)
{
    const struct { const char *what; const char *id; std::string body;
                   size_t samples; unsigned rate; bool builds;
                   unsigned size, frames; } cases[] = {
        { "`clm ', its text running to the chunk's end", "clm ",
          "<!>1024", 4096, 44100, true, 1024, 4 },
        { "...and with more after the length, and ended", "clm ",
          std::string("<!>2048 10000000 wavetable") +
          '\0', 8192, 44100, true, 2048, 4 },
        { "`srge'", "srge", word32(1) + word32(256), 2048, 48000, true,
          256, 8 },
        { "no chunk, 600 samples: one cycle", NULL, "", 600, 44100, true,
          600, 1 },
        { "no chunk, 8192 samples: four of 2048", NULL, "", 8192, 44100,
          true, 2048, 4 },
        { "frames of 5000: refused", "clm ", "<!>5000", 10000, 44100, false,
          0, 0 },
        { "two and a half frames: two", "clm ", "<!>256", 640, 44100, true,
          256, 2 },
        { "300 frames: the first 256", "clm ", "<!>32", 9600, 44100, true,
          32, 256 },
    };

    for (const auto &c : cases)
    {
        const std::string path = dir + "/table.wav";
        std::vector<float> x = saw(c.samples, 8);
        thSampleData data;
        thWaveTable table;
        std::string why;

        if (!writeWav(path, x, c.rate, c.id, c.body) ||
            !thWavRead(path, 0, data, why))
        {
            check(false, std::string(c.what) + ": the file reads: " + why);
            continue;
        }

        const bool built = thWaveTableBuild(data.frames.data(),
                                            data.frames.size(), data.cycle,
                                            table, why);

        check(data.frames.size() == c.samples && built == c.builds &&
              (!built || (table.size == c.size && table.frames == c.frames)),
              std::string(c.what) + ": " + std::to_string(data.frames.size()) +
              " samples as recorded, frames of " + std::to_string(table.size) +
              ", " + std::to_string(table.frames) + " of them" +
              (built ? "" : ", refused: " + why));
    }

    remove((dir + "/table.wav").c_str());
}

/* The 4-point read stepped as a note reads it, the level the highest
   whose top harmonic fits: how far under the fundamental the worst bin
   that is no harmonic of the note comes out. */
static double images (const thWaveTable &table, double hz, unsigned rate)
{
    size_t k = 0;

    while (k + 1 < table.levels.size() &&
           table.levels[k].harmonics * hz >= rate / 2.0)
        k++;

    const size_t n = 1 << 14;
    std::vector<double> y(n);
    double phase = 0;

    for (size_t i = 0; i < n; i++)
    {
        y[i] = thWaveRead(table.levels[k], 0, phase);
        phase += hz / rate;
        phase -= floor(phase);
    }

    /* Windowed by Blackman-Harris, whose sidelobes are 92 dB down, so one
       harmonic's leakage is not mistaken for an image. */
    for (size_t i = 0; i < n; i++)
    {
        const double a = 2 * M_PI * (double)i / n;

        y[i] *= 0.35875 - 0.48829 * cos(a) + 0.14128 * cos(2 * a) -
                0.01168 * cos(3 * a);
    }

    const double binHz = (double)rate / n;
    double fundamental = 0, worst = 0;
    std::vector<double> im(n, 0.0);
    const thWaveTwiddles &tw = thWaveTwiddleTable();

    thRealFft(y, im, 14, tw.wr, tw.wi, ((size_t)1 << THINK_WT_FFT_BITS) / n);

    for (size_t b = 1; b < n / 2; b++)
    {
        const double mag = sqrt(y[b] * y[b] + im[b] * im[b]);
        const double f = b * binHz;
        const double near = fabs(f / hz - floor(f / hz + 0.5)) * hz;

        if (fabs(f - hz) < 2 * binHz)
            fundamental = fmax(fundamental, mag);
        else if (near > 6 * binHz)
            worst = fmax(worst, mag);
    }

    return 20 * log10(worst / fundamental);
}

static void reading (void)
{
    const size_t n = 2048;
    std::vector<float> x = saw(n, 1023);
    thWaveTable table;
    std::string why;

    if (!thWaveTableBuild(x.data(), x.size(), 0, table, why))
    {
        check(false, "a saw builds: " + why);
        return;
    }

    const double notes[] = { 110, 344, 1000, 5000, 10000 };

    for (double hz : notes)
    {
        const double db = images(table, hz, 44100);

        printf("a saw at %g Hz: images %.1f dB under the fundamental\n", hz,
               -db);
        check(db < THINK_WT_IMAGES_DB,
              "a saw read at " + num(hz) + " Hz has its images " + num(-db) +
              " dB under its fundamental, not the " +
              num(-THINK_WT_IMAGES_DB) + " it should");
    }
}

int main (int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: wavetablecheck SCRATCHDIR\n");
        return 2;
    }

    levels();
    samePhase();
    oddLength();
    reader(argv[1]);
    reading();

    if (failures == 0)
        printf("each level holds what it says, and reads under its images\n");

    return failures ? 1 : 0;
}
