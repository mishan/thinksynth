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

#ifndef TH_VIDEOFILE_H
#define TH_VIDEOFILE_H 1

/*
 * A video synth's frames, written as a .y4m: YUV4MPEG2, every pixel's Y,
 * Cb and Cr at full resolution (C444), in BT.601's limited range, which is
 * what a reader assumes of a .y4m that says nothing more. No library: mpv
 * plays it and ffmpeg reads it, and a frame's bytes are the same on every
 * platform. A frame is three planes of R, G and B, 0 black to 1 full; past
 * either end is clamped, NaN is black and infinity white.
 */

#include <stdio.h>

#include <string>
#include <vector>

#include "think.h"

class THINK_API thVideoFile
{
public:
    thVideoFile (void) {}
    ~thVideoFile (void);

    thVideoFile (const thVideoFile &) = delete;
    thVideoFile &operator= (const thVideoFile &) = delete;

    /* False, with a sentence, where the file cannot be made. */
    bool open (const std::string &path, int width, int height, int fps,
               std::string &why);

    /* One frame: `width' x `height' samples each of R, G and B, row by
       row. */
    bool write (const float *r, const float *g, const float *b,
                std::string &why);

    /* False where the last of it did not reach the disk. */
    bool close (std::string &why);

private:
    FILE *file_ = NULL;
    std::string path_;
    int width_ = 0, height_ = 0;
    std::vector<unsigned char> planes_;
};

#endif /* TH_VIDEOFILE_H */
