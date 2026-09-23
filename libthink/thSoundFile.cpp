/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "thSoundFile.h"
#include "thSynth.h"
#include "thSynthTree.h"
#include "thFFT.h"

using std::string;
using std::vector;

namespace thsound {

static unsigned int le (const unsigned char *p, int bytes)
{
    unsigned int v = 0;

    for (int i = 0; i < bytes; i++)
        v |= (unsigned int)p[i] << (8 * i);

    return v;
}

/* Windowed-sinc resampling, 32 taps a side, cut off below the lower of the
   two Nyquists. A target at 48 kHz is the usual case and a comparison at
   the synth's rate is the only comparison there is. */
void resample (vector<float> &x, double from, double to)
{
    const int TAPS = 32;
    const double ratio = to / from;
    const double cutoff = ratio < 1 ? ratio : 1.0;
    const size_t n = (size_t)(x.size() * ratio);
    vector<float> y(n);

    for (size_t i = 0; i < n; i++)
    {
        const double at = i / ratio;
        const long center = (long)floor(at);
        double acc = 0, gain = 0;

        for (long k = center - TAPS + 1; k <= center + TAPS; k++)
        {
            const double t = at - k;
            const double w = 0.5 + 0.5 * cos(thv::FFT_PI * t / TAPS);
            const double sinc = t == 0 ? 1.0
                : sin(thv::FFT_PI * cutoff * t) / (thv::FFT_PI * cutoff * t);
            const double h = w * sinc * cutoff;

            gain += h;

            if (k >= 0 && k < (long)x.size())
                acc += h * x[k];
        }

        y[i] = (float)(gain > 0 ? acc / gain : 0);
    }

    x.swap(y);
}

/* The note a recording is at, by YIN over the half second after its
   onset, or -1 for a sound with no pitch -- a drum, noise. Half a second
   because a note's attack is where the pitch is not yet what it will be. */
int detectNote (const vector<float> &x, double rateHz)
{
    const int rate = (int)rateHz;
    const int LO = rate / 1200, HI = rate / 30;   /* 30 Hz .. 1.2 kHz */
    const size_t W = (size_t)rate / 2;

    double peak = 0;

    for (size_t i = 0; i < x.size(); i++)
        peak = std::max(peak, (double)fabs(x[i]));

    size_t onset = 0;

    while (onset < x.size() && fabs(x[onset]) < peak * 0.05)
        onset++;

    /* Past the attack, and no further than the sound goes. */
    size_t start = onset + (size_t)rate / 20;

    if (start + W + HI > x.size())
    {
        if (x.size() < W + HI + 1)
            return -1;

        start = x.size() - W - HI - 1;
    }

    /* Cumulative-mean-normalized difference; the first dip under the
       threshold, refined by a parabola. */
    vector<double> d(HI + 1, 0.0);

    for (int tau = 1; tau <= HI; tau++)
    {
        double acc = 0;

        for (size_t i = start; i < start + W; i++)
        {
            const double diff = (double)x[i] - x[i + tau];

            acc += diff * diff;
        }

        d[tau] = acc;
    }

    double running = 0;
    vector<double> cmnd(HI + 1, 1.0);

    for (int tau = 1; tau <= HI; tau++)
    {
        running += d[tau];
        cmnd[tau] = running > 0 ? d[tau] * tau / running : 1.0;
    }

    int best = -1;

    for (int tau = LO; tau < HI; tau++)
        if (cmnd[tau] < 0.15)
        {
            while (tau + 1 < HI && cmnd[tau + 1] < cmnd[tau])
                tau++;

            best = tau;
            break;
        }

    if (best < 0)
        return -1;

    double period = best;

    if (best > 1 && best + 1 <= HI)
    {
        const double a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
        const double den = a - 2 * b + c;

        if (den != 0)
            period += 0.5 * (a - c) / den;
    }

    const double hz = rate / period;

    return (int)lrint(69 + 12 * log2(hz / 440.0));
}

/* PCM of 16, 24 or 32 bits or 32-bit float, mixed to mono and brought to
   the synth's rate. */
bool readWav (const string &path, vector<float> &mono, string &why, double outRate)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::ostringstream ss;

    ss << in.rdbuf();

    const string data = ss.str();
    const unsigned char *d = (const unsigned char *)data.data();

    if (!in || data.size() < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4))
    {
        why = "not a WAV file";
        return false;
    }

    unsigned int format = 0, channels = 0, rate = 0, bits = 0;

    for (size_t at = 12; at + 8 <= data.size(); )
    {
        const size_t size = le(d + at + 4, 4);
        const unsigned char *body = d + at + 8;

        if (at + 8 + size > data.size())
            break;

        if (!memcmp(d + at, "fmt ", 4) && size >= 16)
        {
            format = le(body, 2);
            channels = le(body + 2, 2);
            rate = le(body + 4, 4);
            bits = le(body + 14, 2);

            /* WAVE_FORMAT_EXTENSIBLE keeps the real format in the first
               two bytes of its GUID. */
            if (format == 0xFFFE && size >= 26)
                format = le(body + 24, 2);
        }
        else if (!memcmp(d + at, "data", 4))
        {
            const unsigned int bytes = bits / 8;

            if (!channels || !bytes ||
                !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) ||
                  (format == 3 && bits == 32)))
            {
                why = "only 16, 24 or 32 bit PCM and 32 bit float are read";
                return false;
            }

            if (rate == 0)
            {
                why = "no sample rate";
                return false;
            }

            for (size_t f = 0; (f + 1) * channels * bytes <= size; f++)
            {
                double acc = 0;

                for (unsigned int c = 0; c < channels; c++)
                {
                    const unsigned char *p = body + (f * channels + c) * bytes;

                    if (format == 3)
                    {
                        float x;

                        memcpy(&x, p, 4);
                        acc += x;
                    }
                    else
                    {
                        /* Sign-extended by shifting the sample to the top
                           of the word first. */
                        const int v = (int)(le(p, (int)bytes) << (32 - bits));

                        acc += (double)v / 2147483648.0;
                    }
                }

                mono.push_back((float)(acc / channels));
            }

            if (rate != outRate)
                resample(mono, rate, outRate);

            return true;
        }

        at += 8 + size + (size & 1);
    }

    why = "no sample data";
    return false;
}

/* 16-bit mono, peaking 3 dB under full scale whatever the render's own
   level was: the distance does not hear gain, so what it found has none
   worth keeping, and three files at three levels cannot be compared by
   ear. */
bool writeWav (const string &path, const vector<float> &mono, double rate)
{
    double peak = 0;

    for (size_t i = 0; i < mono.size(); i++)
        if (fabs((double)mono[i]) > peak)
            peak = fabs((double)mono[i]);

    const double gain = peak > 0 ? 0.708 * 32767.0 / peak : 0;
    const unsigned int bytes = (unsigned int)mono.size() * 2;

    std::ofstream out(path.c_str(), std::ios::binary);

    unsigned char h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
                            0,0,0,0, 0,0,0,0, 2,0, 16,0,
                            'd','a','t','a', 0,0,0,0 };
    const unsigned int fields[4][2] = { { 4, 36 + bytes },
                                        { 24, (unsigned int)rate },
                                        { 28, (unsigned int)rate * 2 },
                                        { 40, bytes } };

    for (int f = 0; f < 4; f++)
        for (int i = 0; i < 4; i++)
            h[fields[f][0] + i] = (unsigned char)(fields[f][1] >> (8 * i));

    out.write((const char *)h, sizeof h);

    for (size_t i = 0; i < mono.size(); i++)
    {
        const int v = (int)lrint((double)mono[i] * gain);
        const unsigned char b[2] = { (unsigned char)(v & 0xff),
                                     (unsigned char)((v >> 8) & 0xff) };

        out.write((const char *)b, 2);
    }

    return (bool)out;
}

bool renderNote (thSynth &synth, const string &dspPath,
                 const vector<std::pair<string, float> > &chanargs,
                 int note, int holdWindows, int tailWindows,
                 vector<float> &mono, const string &effectPath)
{
    /* Every render from the same place: a patch with a noise source in
       it renders the same twice, which a comparison of two renders needs
       and a search of one patch needs more. */
    srand(1);

    thSynthTree *tree = synth.loadTree(dspPath, 0, 100);

    if (tree == NULL)
        return false;

    /* After the patch, which builds a new channel and takes any effect
       with it. */
    if (!effectPath.empty() && synth.loadEffect(effectPath, 0) == NULL)
        return false;

    for (size_t i = 0; i < chanargs.size(); i++)
    {
        thArg *arg = synth.getChanArg(0, chanargs[i].first);

        if (arg != NULL)
            arg->setValue(chanargs[i].second);
    }

    synth.addNote(0, note, 100);

    const int channels = synth.audioChannelCount();
    const int len = synth.getWindowlen();

    mono.clear();
    mono.reserve((size_t)(holdWindows + tailWindows) * len);

    for (int w = 0; w < holdWindows + tailWindows; w++)
    {
        if (w == holdWindows)
            synth.delNote(0, note);

        synth.process();

        const float *buf = synth.getOutput();

        for (int i = 0; i < len; i++)
        {
            double acc = 0;

            for (int c = 0; c < channels; c++)
                acc += buf[(size_t)c * len + i];

            mono.push_back((float)(acc / channels));
        }
    }

    return true;
}

} /* namespace thsound */
