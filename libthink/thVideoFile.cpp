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

#include <errno.h>
#include <math.h>
#include <string.h>

#include "thVideoFile.h"

/* One component, rounded to the nearest step and kept in its range. */
static unsigned char
level (double v, double lo, double hi)
{
    return (unsigned char)lround(v < lo ? lo : v > hi ? hi : v);
}

/* 0 to 1; NaN is black. */
static double
unit (float v)
{
    return v >= 1 ? 1 : v > 0 ? v : 0;
}

thVideoFile::~thVideoFile (void)
{
    std::string why;

    close(why);
}

bool
thVideoFile::open (const std::string &path, int width, int height, int fps,
                   std::string &why)
{
    if (width < 1 || height < 1 || fps < 1)
    {
        why = "a frame of " + std::to_string(width) + "x" +
              std::to_string(height) + " at " + std::to_string(fps) +
              " a second is no picture";
        return false;
    }

    close(why);
    file_ = fopen(path.c_str(), "wb");
    if (file_ == NULL)
    {
        why = path + ": " + strerror(errno);
        return false;
    }

    path_ = path;
    width_ = width;
    height_ = height;
    planes_.resize(3 * (size_t)width * height);
    if (fprintf(file_, "YUV4MPEG2 W%d H%d F%d:1 Ip A1:1 C444\n",
                width, height, fps) < 0)
    {
        why = path + ": " + strerror(errno);
        return false;
    }

    return true;
}

bool
thVideoFile::write (const float *r, const float *g, const float *b,
                    std::string &why)
{
    if (file_ == NULL)
    {
        why = "no file is open";
        return false;
    }

    const size_t n = (size_t)width_ * height_;
    unsigned char *y = planes_.data(), *cb = y + n, *cr = cb + n;

    for (size_t i = 0; i < n; i++)
    {
        const double R = unit(r[i]), G = unit(g[i]), B = unit(b[i]);
        const double luma = 0.299 * R + 0.587 * G + 0.114 * B;

        y[i] = level(16 + 219 * luma, 16, 235);
        cb[i] = level(128 + 224 * (B - luma) / 1.772, 16, 240);
        cr[i] = level(128 + 224 * (R - luma) / 1.402, 16, 240);
    }

    if (fputs("FRAME\n", file_) < 0 ||
        fwrite(planes_.data(), 1, planes_.size(), file_) != planes_.size())
    {
        why = path_ + ": " + strerror(errno);
        return false;
    }

    return true;
}

bool
thVideoFile::close (std::string &why)
{
    if (file_ == NULL)
        return true;

    const bool ok = fclose(file_) == 0;

    file_ = NULL;
    if (!ok)
        why = path_ + ": " + strerror(errno);

    return ok;
}
