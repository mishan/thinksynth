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

#ifndef TH_SOUNDFEAT_H
#define TH_SOUNDFEAT_H 1

/*
 * What a rendered note sounds like, as numbers two renders can be compared
 * by.
 *
 * A search that is judged on its audio needs a distance that is small when
 * two sounds are alike *to an ear*, and sample-by-sample difference is not
 * that: two identical saws a quarter cycle apart differ at every sample.
 * So the comparison is between magnitudes, which throw phase away:
 *
 *   spectral   log spectrograms at several transform sizes, compared
 *              frame by frame. One size cannot do it: 512 points sees an
 *              attack and smears partials together, 4096 separates partials
 *              and smears the attack. A sound matches when all three agree.
 *              Log, because loudness is, and a distance in linear magnitude
 *              hears nothing but the fundamental.
 *
 *              A fourth layer is for pitch in the bass, which mel bands
 *              are deaf to: below a few hundred hertz they are sixty hertz
 *              wide, and a kick whose tail settles on 46 Hz and one that
 *              settles on 48.6 fall in the same band at every size above.
 *              An ear hears that semitone as a different drum. So: 16384
 *              points, and bands an eighth of an octave apart from 27.5 to
 *              880 Hz, where a semitone is most of a band.
 *
 *   envelope   RMS in dB below the sound's own peak, every 10 ms. The
 *              spectrograms are taken after loudness normalization and so
 *              know little about how a note swells and dies; this term is
 *              where an attack time is heard.
 *
 * Both are taken from a signal that has been trimmed to its onset and scaled
 * to unit RMS, so neither a few ms of latency nor an output gain is
 * something a search has to discover. Gain is the one parameter that can
 * always be fixed afterwards by arithmetic.
 *
 * The terms are kept apart in Distance as well as summed, because a search
 * with several objectives wants them apart and because a number that is the
 * sum of two things cannot be debugged.
 *
 * A render that is not a sound at all -- non-finite, silent, or all DC --
 * is reported by `usable' and has no features. The caller decides what that
 * costs; here it is only detected.
 */

#include <math.h>

#include <vector>

#include "fftr.h"

namespace thsound {

using std::vector;

const int MEL_BANDS = 40;
const int RESOLUTIONS = 4;
const unsigned int FFT_ORDERS[RESOLUTIONS] = { 9, 11, 12, 14 };

/* The last of those is the bass layer, banded in octaves, not mels. */
const int BASS_RESOLUTION = 3;
const double BASS_LO_HZ = 27.5, BASS_HI_HZ = 880.0;

/* A band's level is 20 log10(magnitude + FLOOR), so it approaches
   FLOOR_DB and never falls through it. A hard floor a long way down was
   tried first and is wrong for anything with a comb in it: a chorus or a
   pulse wave is notches, a notch is forty dB deep for a frame and gone the
   next, and two sounds with the same chorus a hair apart in rate then
   differ by forty dB wherever a notch is. An ear does not hear the bottom
   of a notch. Signals are at unit RMS by the time this applies. */
const double FLOOR_DB = -50.0;
const double AUDIBLE_DB = -40.0;
const double ENV_FLOOR_DB = -60.0;

enum Verdict {
    USABLE = 0,
    NOT_FINITE,
    SILENT,
    ALL_DC
};

inline const char *verdictName (Verdict v)
{
    switch (v)
    {
    case USABLE:     return "usable";
    case NOT_FINITE: return "not finite";
    case SILENT:     return "silent";
    case ALL_DC:     return "all DC";
    }

    return "?";
}

struct Features {
    Verdict verdict;

    /* mel[r] is frames x MEL_BANDS, row-major, in dB. */
    vector<float> mel[RESOLUTIONS];
    int frames[RESOLUTIONS];

    /* dB below the loudest 10 ms, one per 10 ms. */
    vector<float> envelope;


    Features (void) : verdict(SILENT)
    {
        for (int r = 0; r < RESOLUTIONS; r++)
            frames[r] = 0;
    }

    bool usable (void) const { return verdict == USABLE; }
};

struct Distance {
    double spectral;    /* mean |dB| per band per frame, over resolutions */
    double envelope;    /* mean |dB| per 10 ms                            */

    Distance (void) : spectral(0), envelope(0) {}

    /* One number, for a search that wants one. Both terms are already in
       dB, so the weight is a statement about taste rather than about
       units: a dB of envelope error matters half as much as a dB of
       spectral error, because the spectrograms hear the envelope too. */
    double total (void) const { return spectral + 0.5 * envelope; }
};

inline double hzToMel (double hz) { return 2595.0 * log10(1.0 + hz / 700.0); }
inline double melToHz (double mel) { return 700.0 * (pow(10.0, mel / 2595.0) - 1.0); }

/* Triangular mel filters over the bins of one transform size. Built once
   per Extractor: a search calls extract() a hundred thousand times. */
class MelBank {
public:
    /* `octaves' spaces the band centers evenly in log frequency between
       `loHz' and `hiHz'; otherwise evenly in mels, from 30 Hz to 16 kHz or
       as far as the rate goes. */
    MelBank (unsigned int bins, double samplerate, bool octaves = false,
             double loHz = 30.0, double hiHz = 16000.0)
        : bins_(bins), weights_((size_t)bins * MEL_BANDS, 0.0f),
          floor_(pow(10.0, FLOOR_DB / 20.0))
    {
        for (int b = 0; b < MEL_BANDS; b++)
            first_[b] = last_[b] = 0;

        if (hiHz > samplerate * 0.5)
            hiHz = samplerate * 0.5;

        const double lo = octaves ? log2(loHz) : hzToMel(loHz);
        const double hi = octaves ? log2(hiHz) : hzToMel(hiHz);
        const double hzPerBin = samplerate * 0.5 / (double)bins;

        for (int b = 0; b < MEL_BANDS; b++)
        {
            double edge[3];

            for (int k = 0; k < 3; k++)
            {
                const double at = lo + (hi - lo) * (b + k) / (MEL_BANDS + 1);

                edge[k] = octaves ? pow(2.0, at) : melToHz(at);
            }

            const double left = edge[0], mid = edge[1], right = edge[2];

            double sum = 0;

            for (unsigned int i = 0; i < bins; i++)
            {
                const double hz = i * hzPerBin;
                double w = 0;

                if (hz > left && hz <= mid)
                    w = (hz - left) / (mid - left);
                else if (hz > mid && hz < right)
                    w = (right - hz) / (right - mid);

                weights_[(size_t)b * bins + i] = (float)w;
                sum += w;
            }

            /* A low band at a short transform can fall between two bins
               and weigh nothing. Give it the nearest bin rather than let
               it read as silence in every sound alike. */
            if (sum <= 0)
            {
                unsigned int nearest = (unsigned int)(mid / hzPerBin + 0.5);

                if (nearest >= bins)
                    nearest = bins - 1;

                weights_[(size_t)b * bins + nearest] = 1.0f;
                sum = 1;
            }

            for (unsigned int i = 0; i < bins; i++)
                weights_[(size_t)b * bins + i] /= (float)sum;

            /* A triangle touches a few bins out of thousands, and apply()
               is the inner loop of every evaluation a search makes. */
            first_[b] = bins;
            last_[b] = 0;

            for (unsigned int i = 0; i < bins; i++)
                if (weights_[(size_t)b * bins + i] > 0)
                {
                    if (i < first_[b]) first_[b] = i;
                    last_[b] = i + 1;
                }
        }
    }

    void apply (const float *mag, float *out) const
    {
        for (int b = 0; b < MEL_BANDS; b++)
        {
            const float *w = &weights_[(size_t)b * bins_];
            double acc = 0;

            for (unsigned int i = first_[b]; i < last_[b]; i++)
                acc += (double)w[i] * (double)mag[i];

            out[b] = (float)(20.0 * log10(acc + floor_));
        }
    }

private:
    unsigned int bins_;
    vector<float> weights_;
    unsigned int first_[MEL_BANDS], last_[MEL_BANDS];
    double floor_;
};

class Extractor {
public:
    explicit Extractor (double samplerate)
        : samplerate_(samplerate)
    {
        for (int r = 0; r < RESOLUTIONS; r++)
        {
            fft_[r] = new thv::FFTR(FFT_ORDERS[r]);
            bank_[r] = r == BASS_RESOLUTION
                     ? new MelBank(fft_[r]->bins(), samplerate, true,
                                   BASS_LO_HZ, BASS_HI_HZ)
                     : new MelBank(fft_[r]->bins(), samplerate);
        }
    }

    ~Extractor (void)
    {
        for (int r = 0; r < RESOLUTIONS; r++)
        {
            delete fft_[r];
            delete bank_[r];
        }
    }

    /* `mono' is the whole render. It is taken by value because it is
       trimmed and scaled in place. */
    void extract (vector<float> mono, Features &out) const
    {
        out = Features();

        double sum = 0, sumSq = 0, peak = 0;

        for (size_t i = 0; i < mono.size(); i++)
        {
            if (!std::isfinite(mono[i]))
            {
                out.verdict = NOT_FINITE;
                return;
            }

            const double a = fabs((double)mono[i]);

            sum += mono[i];
            sumSq += (double)mono[i] * mono[i];

            if (a > peak)
                peak = a;
        }

        if (mono.empty() || peak < 1e-5)
        {
            out.verdict = SILENT;
            return;
        }

        const double mean = sum / (double)mono.size();
        const double rms = sqrt(sumSq / (double)mono.size());
        const double ac = sqrt(rms * rms - mean * mean > 0
                               ? rms * rms - mean * mean : 0);

        /* An offset an output stage would block, with nothing riding it. */
        if (ac < 0.05 * fabs(mean))
        {
            out.verdict = ALL_DC;
            return;
        }

        /* The onset: the first sample within 40 dB of the peak. What came
           before it is latency, and a frame-by-frame comparison would
           otherwise charge a 5 ms delay at every frame of the note. */
        size_t onset = 0;

        while (onset < mono.size() && fabs((double)mono[onset] - mean) < peak * 0.01)
            onset++;

        const size_t len = mono.size();

        /* Shifted rather than shortened, and padded with silence, so that
           two renders of the same length give the same number of frames
           whatever their onsets were. */
        for (size_t i = 0; i < len; i++)
            mono[i] = i + onset < len
                    ? (float)(((double)mono[i + onset] - mean) / ac)
                    : 0.0f;

        for (int r = 0; r < RESOLUTIONS; r++)
            spectrogram(mono, r, out);

        envelope(mono, out);

        out.verdict = USABLE;
    }

    /* Two feature sets of the same render length. Frame counts that
       disagree are compared over the shorter, which a target read from a
       file will need and two renders never do.

       Only frames where one of the two is sounding are counted. Two
       drum hits are both silent for most of a render, silence matches
       silence exactly, and averaged over every frame a kick came out a
       dB and a half from a clave. */
    static Distance distance (const Features &a, const Features &b)
    {
        Distance d;

        for (int r = 0; r < RESOLUTIONS; r++)
        {
            const int frames = a.frames[r] < b.frames[r]
                             ? a.frames[r] : b.frames[r];
            double acc = 0;
            size_t counted = 0;

            for (int f = 0; f < frames; f++)
            {
                const float *fa = &a.mel[r][(size_t)f * MEL_BANDS];
                const float *fb = &b.mel[r][(size_t)f * MEL_BANDS];

                if (!audible(fa) && !audible(fb))
                    continue;

                for (int m = 0; m < MEL_BANDS; m++)
                    acc += fabs((double)fa[m] - (double)fb[m]);

                counted += MEL_BANDS;
            }

            if (counted)
                d.spectral += acc / (double)counted / RESOLUTIONS;
        }

        const size_t n = a.envelope.size() < b.envelope.size()
                       ? a.envelope.size() : b.envelope.size();
        double acc = 0;

        size_t counted = 0;

        for (size_t i = 0; i < n; i++)
        {
            if (a.envelope[i] <= ENV_FLOOR_DB && b.envelope[i] <= ENV_FLOOR_DB)
                continue;

            acc += fabs((double)a.envelope[i] - (double)b.envelope[i]);
            counted++;
        }

        if (counted)
            d.envelope = acc / (double)counted;

        return d;
    }

private:
    Extractor (const Extractor &);
    Extractor &operator= (const Extractor &);

    static bool audible (const float *frame)
    {
        for (int m = 0; m < MEL_BANDS; m++)
            if (frame[m] > AUDIBLE_DB)
                return true;

        return false;
    }

    struct At {
        const float *p;
        size_t avail;

        float operator() (unsigned int i) const { return i < avail ? p[i] : 0.0f; }
    };

    void spectrogram (const vector<float> &x, int r, Features &out) const
    {
        const unsigned int n = fft_[r]->size();
        const unsigned int hop = n / 4;

        vector<float> mag(fft_[r]->bins());

        out.frames[r] = 0;

        for (size_t start = 0; start < x.size(); start += hop)
        {
            const At at = { &x[start], x.size() - start };

            fft_[r]->magnitude(at, &mag[0]);

            out.mel[r].resize(out.mel[r].size() + MEL_BANDS);
            bank_[r]->apply(&mag[0],
                            &out.mel[r][(size_t)out.frames[r] * MEL_BANDS]);
            out.frames[r]++;
        }
    }

    void envelope (const vector<float> &x, Features &out) const
    {
        const size_t hop = (size_t)(samplerate_ * 0.010);
        double loudest = 0;

        for (size_t start = 0; start + hop <= x.size(); start += hop)
        {
            double acc = 0;

            for (size_t i = 0; i < hop; i++)
                acc += (double)x[start + i] * x[start + i];

            const double rms = sqrt(acc / (double)hop);

            out.envelope.push_back((float)rms);

            if (rms > loudest)
                loudest = rms;
        }

        for (size_t i = 0; i < out.envelope.size(); i++)
        {
            const double rel = out.envelope[i] / loudest;
            const double db = rel > 0 ? 20.0 * log10(rel) : ENV_FLOOR_DB;

            out.envelope[i] = (float)(db < ENV_FLOOR_DB ? ENV_FLOOR_DB : db);
        }
    }

    double samplerate_;

    thv::FFTR *fft_[RESOLUTIONS];
    MelBank *bank_[RESOLUTIONS];
};

} /* namespace thsound */

#endif /* TH_SOUNDFEAT_H */
