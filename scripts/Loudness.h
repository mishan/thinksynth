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
 * Loudness: integrated loudness in LUFS, as ITU-R BS.1770-4 and EBU R 128
 * measure it -- what a streaming service turns a track up or down to, and
 * so the number two pieces should agree on before they sit in a playlist.
 *
 * Each channel is K-weighted (a high shelf of about +4 dB above 1.5 kHz,
 * for the head, then a high-pass at 38 Hz), squared, and summed across
 * channels; the sum is averaged over 400 ms blocks overlapping by three
 * quarters. A block quieter than -70 LUFS is silence and does not count,
 * and then neither does one more than 10 LU under the average of the
 * rest, so a quiet intro or a tail does not drag a piece's number down.
 * The filters are designed for the rate in use, as libebur128's are, so
 * 44.1 kHz needs no table of its own.
 */

#ifndef TH_LOUDNESS_H
#define TH_LOUDNESS_H 1

#include <math.h>

#include <vector>

class Loudness {
public:
    Loudness (int channels, double rate)
        : rate_(rate), chans_(channels), state_(channels),
          blockLen_((size_t)(rate * 0.1 + 0.5))
    {
        design(1681.974450955533, 3.999843853973347, 0.7071752369554196,
               shelf_, true);
        design(38.13547087602444, 0, 0.5003270373238773, highpass_, false);
    }

    /* One frame: a sample of each channel. */
    void add (const float *frame)
    {
        for (int c = 0; c < chans_; c++)
        {
            const double y = filter(highpass_, state_[c].hp,
                                    filter(shelf_, state_[c].sh, frame[c]));

            sum_ += y * y;
        }

        if (++fill_ == blockLen_)
        {
            quarters_.push_back(sum_ / blockLen_);
            sum_ = 0;
            fill_ = 0;
        }
    }

    /* Integrated loudness, or -HUGE_VAL for nothing above the absolute
       gate. */
    double integrated (void) const
    {
        std::vector<double> blocks;

        /* A 400 ms block is four 100 ms quarters, a quarter apart. */
        for (size_t q = 3; q < quarters_.size(); q++)
            blocks.push_back((quarters_[q - 3] + quarters_[q - 2] +
                              quarters_[q - 1] + quarters_[q]) / 4);

        const double gate = gated(blocks, power(-70.0));

        /* 10 LU under is a tenth of the power. */
        return gate > 0 ? lufs(gated(blocks, gate / 10)) : -HUGE_VAL;
    }

    static double lufs (double meanSquare)
    {
        return meanSquare > 0 ? -0.691 + 10 * log10(meanSquare) : -HUGE_VAL;
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };
    struct Taps { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    struct Chan { Taps sh, hp; };

    /* The mean square that reads `l' LUFS. */
    static double power (double l) { return pow(10.0, (l + 0.691) / 10); }

    /* The mean of the blocks at or above `floor', or 0 for none. */
    static double gated (const std::vector<double> &blocks, double floor)
    {
        double sum = 0;
        size_t n = 0;

        for (double z : blocks)
            if (z >= floor)
            {
                sum += z;
                n++;
            }

        return n ? sum / n : 0;
    }

    void design (double f0, double gainDb, double q, Biquad &f, bool shelf)
    {
        const double k = tan(M_PI * f0 / rate_);
        const double a0 = 1 + k / q + k * k;

        f.a1 = 2 * (k * k - 1) / a0;
        f.a2 = (1 - k / q + k * k) / a0;

        if (shelf)
        {
            const double vh = pow(10.0, gainDb / 20);
            const double vb = pow(vh, 0.4996667741545416);

            f.b0 = (vh + vb * k / q + k * k) / a0;
            f.b1 = 2 * (k * k - vh) / a0;
            f.b2 = (vh - vb * k / q + k * k) / a0;
        }
        else
        {
            f.b0 = 1;
            f.b1 = -2;
            f.b2 = 1;
        }
    }

    static double filter (const Biquad &f, Taps &t, double x)
    {
        const double y = f.b0 * x + f.b1 * t.x1 + f.b2 * t.x2 -
                         f.a1 * t.y1 - f.a2 * t.y2;

        t.x2 = t.x1;
        t.x1 = x;
        t.y2 = t.y1;
        t.y1 = y;

        return y;
    }

    double rate_;
    int chans_;
    std::vector<Chan> state_;
    Biquad shelf_, highpass_;
    size_t blockLen_, fill_ = 0;
    double sum_ = 0;
    std::vector<double> quarters_;
};

#endif /* TH_LOUDNESS_H */
