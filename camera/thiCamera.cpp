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

#include <setjmp.h>
#include <stdio.h>

#include <jpeglib.h>

#include "thiCamera.h"

namespace {

/* libjpeg's default for an error is exit(); this jumps back instead, and
   keeps its warnings off stderr, where a camera's would scroll for ever. */
struct JpegError
{
    jpeg_error_mgr mgr;
    jmp_buf        jump;
};

void jpegFail (j_common_ptr c)
{
    longjmp(((JpegError *)c->err)->jump, 1);
}

void jpegQuiet (j_common_ptr)
{
}

}

bool thiDecodeLuma (const uint8_t *jpeg, size_t len, int maxWidth,
                    std::vector<uint8_t> &luma, int &width, int &height)
{
    jpeg_decompress_struct c;
    JpegError e;

    c.err = jpeg_std_error(&e.mgr);
    e.mgr.error_exit = jpegFail;
    e.mgr.output_message = jpegQuiet;

    if (setjmp(e.jump))
    {
        jpeg_destroy_decompress(&c);
        return false;
    }

    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, jpeg, (unsigned long)len);
    jpeg_read_header(&c, TRUE);

    unsigned int denom = 1;

    while (denom < 8 && (c.image_width + denom - 1) / denom > (unsigned int)maxWidth)
        denom *= 2;

    c.scale_num = 1;
    c.scale_denom = denom;
    c.out_color_space = JCS_GRAYSCALE;
    c.dct_method = JDCT_IFAST;
    jpeg_start_decompress(&c);

    width = (int)c.output_width;
    height = (int)c.output_height;
    luma.resize((size_t)width * height);

    while (c.output_scanline < c.output_height)
    {
        JSAMPROW row = &luma[(size_t)c.output_scanline * width];

        jpeg_read_scanlines(&c, &row, 1);
    }

    jpeg_finish_decompress(&c);

    /* A frame cut short decodes, gray where it ran out; that is a jump,
       not a picture. */
    const bool whole = e.mgr.num_warnings == 0;

    jpeg_destroy_decompress(&c);
    return whole;
}

thiCamera::thiCamera (const Connect &connect, const std::function<void ()> &wake, int retryMs)
    : connect_(connect), wake_(wake), retryMs_(retryMs), stop_(false), notified_(false)
{
    thread_ = std::thread(&thiCamera::run, this);
}

thiCamera::~thiCamera ()
{
    {
        std::lock_guard<std::mutex> hold(lock_);
        stop_ = true;
    }
    stopped_.notify_all();
    thread_.join();
}

bool thiCamera::drain (std::vector<thiAxes> &frames)
{
    std::lock_guard<std::mutex> hold(lock_);

    frames.swap(frames_);
    frames_.clear();
    notified_ = false;
    return !frames.empty();
}

std::string thiCamera::status () const
{
    std::lock_guard<std::mutex> hold(lock_);

    return status_;
}

void thiCamera::publish (const thiAxes &a)
{
    {
        std::lock_guard<std::mutex> hold(lock_);

        frames_.push_back(a);
        if (frames_.size() > KEPT)
            frames_.erase(frames_.begin());
    }

    if (!notified_.exchange(true))
        wake_();
}

void thiCamera::setStatus (const std::string &status)
{
    std::lock_guard<std::mutex> hold(lock_);

    status_ = status;
}

void thiCamera::rest (int ms)
{
    std::unique_lock<std::mutex> hold(lock_);

    stopped_.wait_for(hold, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
}

void thiCamera::run ()
{
    /* Long enough to be idle, short enough that a stop waits no longer. */
    static const int WAIT_MS = 100;

    std::unique_ptr<thiSource> source;
    std::unique_ptr<thMotion> motion;
    std::vector<uint8_t> jpeg, luma;
    double last = 0;

    while (!stop_)
    {
        std::string status;

        if (!source)
        {
            source = connect_(status);
            if (!source)
            {
                setStatus(status.empty() ? "no camera" : status);
                rest(retryMs_);
                continue;
            }
            setStatus("");
        }

        double seconds = 0;

        if (!source->next(jpeg, seconds, WAIT_MS, status))
        {
            if (status.empty())
                continue;

            source.reset();
            setStatus(status);

            thiAxes gone;

            gone.seconds = last;
            publish(gone);
            rest(retryMs_);
            continue;
        }

        int width = 0, height = 0;

        if (!thiDecodeLuma(jpeg.data(), jpeg.size(), WIDTH, luma, width, height)
            || width < 3 || height < 3)
            continue;

        if (!motion || motion->width() != width || motion->height() != height)
            motion.reset(new thMotion(width, height));

        motion->feed(luma.data(), width, seconds);

        thiAxes a;

        for (int i = 0; i < thMotion::AXES; i++)
            a.axis[i] = motion->axis((thMotion::Axis)i);
        a.seconds = last = seconds;
        a.connected = true;
        publish(a);
    }
}
