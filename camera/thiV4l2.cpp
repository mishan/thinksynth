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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <algorithm>
#include <vector>

#include "thiV4l2.h"

namespace {

int xioctl (int fd, unsigned long request, void *arg)
{
    int r;

    do
        r = ioctl(fd, request, arg);
    while (r < 0 && errno == EINTR);

    return r;
}

bool capturesMjpeg (int fd)
{
    v4l2_capability cap = {};

    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return false;

    /* A camera's metadata node answers QUERYCAP too, without capture. */
    const unsigned caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps
                                                                  : cap.capabilities;

    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING))
        return false;

    v4l2_fmtdesc f = {};

    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (f.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &f) == 0; f.index++)
        if (f.pixelformat == V4L2_PIX_FMT_MJPEG)
            return true;

    return false;
}

/* The shortest frame interval the camera has at this size, as a rate. */
v4l2_fract fastest (int fd, unsigned width, unsigned height)
{
    v4l2_fract best = { 1, 30 };
    v4l2_frmivalenum iv = {};

    iv.pixel_format = V4L2_PIX_FMT_MJPEG;
    iv.width = width;
    iv.height = height;
    for (iv.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) == 0; iv.index++)
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE
            && (double)iv.discrete.numerator / iv.discrete.denominator
               < (double)best.numerator / best.denominator)
            best = iv.discrete;

    return best;
}

class V4l2Source : public thiSource
{
public:
    V4l2Source (int fd, const std::string &device) : fd_(fd), device_(device), dynamic_(-1) { }

    ~V4l2Source () override
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        for (auto &b : buffers_)
            munmap(b.start, b.length);
        if (dynamic_ >= 0)
        {
            v4l2_control c = { V4L2_CID_EXPOSURE_AUTO_PRIORITY, dynamic_ };

            xioctl(fd_, VIDIOC_S_CTRL, &c);
        }
        close(fd_);
    }

    bool start (std::string &status)
    {
        v4l2_format f = {};

        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        f.fmt.pix.width = 640;
        f.fmt.pix.height = 480;
        f.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        f.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(fd_, VIDIOC_S_FMT, &f) < 0)
            return fail(status, errno == EBUSY ? "in use" : "can't set Motion-JPEG");
        if (f.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
            return fail(status, "can't set Motion-JPEG");

        v4l2_streamparm p = {};

        p.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        p.parm.capture.timeperframe = fastest(fd_, f.fmt.pix.width, f.fmt.pix.height);
        xioctl(fd_, VIDIOC_S_PARM, &p);

        v4l2_control c = { V4L2_CID_EXPOSURE_AUTO_PRIORITY, 0 };

        if (xioctl(fd_, VIDIOC_G_CTRL, &c) == 0)
        {
            const int was = c.value;

            c.value = 0;
            if (xioctl(fd_, VIDIOC_S_CTRL, &c) == 0)
                dynamic_ = was;
        }

        v4l2_requestbuffers rb = {};

        rb.count = 4;
        rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        rb.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2)
            return fail(status, "no capture buffers");

        for (unsigned i = 0; i < rb.count; i++)
        {
            v4l2_buffer b = {};

            b.type = rb.type;
            b.memory = rb.memory;
            b.index = i;
            if (xioctl(fd_, VIDIOC_QUERYBUF, &b) < 0)
                return fail(status, "no capture buffers");

            void *start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);

            if (start == MAP_FAILED)
                return fail(status, "no capture buffers");
            buffers_.push_back({ start, b.length });
            if (xioctl(fd_, VIDIOC_QBUF, &b) < 0)
                return fail(status, "no capture buffers");
        }

        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
            return fail(status, "can't start streaming");

        return true;
    }

    bool next (std::vector<uint8_t> &jpeg, double &seconds, int waitMs,
               std::string &status) override
    {
        pollfd pfd = { fd_, POLLIN, 0 };
        const int r = poll(&pfd, 1, waitMs);

        if (r == 0 || (r < 0 && errno == EINTR))
            return false;
        if (r < 0 || pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return fail(status, "gone");

        v4l2_buffer b = {};

        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &b) < 0)
            return errno == EAGAIN ? false : fail(status, "gone");

        if (b.index >= buffers_.size())
            return fail(status, "gone");

        const uint8_t *data = (const uint8_t *)buffers_[b.index].start;

        jpeg.assign(data, data + std::min((size_t)b.bytesused, buffers_[b.index].length));

        /* The driver's capture time when it keeps one on the monotonic
           clock, as a webcam's does: not when this thread got round to it.
           A driver that doesn't is stamped on arrival. */
        if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
            seconds = b.timestamp.tv_sec + b.timestamp.tv_usec / 1e6;
        else
        {
            timespec now;

            clock_gettime(CLOCK_MONOTONIC, &now);
            seconds = now.tv_sec + now.tv_nsec / 1e9;
        }

        if (xioctl(fd_, VIDIOC_QBUF, &b) < 0)
            return fail(status, "gone");

        return true;
    }

private:
    struct Buffer { void *start; size_t length; };

    int fd_;
    std::string device_;
    int dynamic_;
    std::vector<Buffer> buffers_;

    bool fail (std::string &status, const char *why)
    {
        status = device_ + ": " + why;
        return false;
    }
};

}

std::unique_ptr<thiSource> thiOpenV4l2 (const std::string &device, std::string &status)
{
    std::vector<std::string> candidates;
    std::string found;

    if (!device.empty())
        candidates.push_back(device);
    else
        for (int i = 0; i < 64; i++)
            candidates.push_back("/dev/video" + std::to_string(i));

    for (const auto &path : candidates)
    {
        const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);

        if (fd < 0)
        {
            if (!device.empty())
                status = path + ": " + strerror(errno);
            continue;
        }
        if (!capturesMjpeg(fd))
        {
            close(fd);
            if (!device.empty())
                status = path + ": not a Motion-JPEG camera";
            continue;
        }

        std::unique_ptr<V4l2Source> source(new V4l2Source(fd, path));

        if (source->start(status))
            return std::move(source);
        if (!device.empty())
            return nullptr;
        /* A camera that is there and won't start -- in use by something
           else, say -- is the reason worth giving. */
        found = status;
    }

    if (device.empty())
        status = found.empty() ? "no Motion-JPEG camera" : found;

    return nullptr;
}
