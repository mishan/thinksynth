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

#ifndef THI_CAMERA_H
#define THI_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "thMotion.h"

/* The luma of a JPEG, decoded at the largest of 1/1, 1/2, 1/4 or 1/8 scale
   that is no wider than maxWidth, or at 1/8 if none is: libjpeg scales in
   the transform, so a 640x480 frame comes out 160x120 for a fraction of a
   full decode. False if it isn't a JPEG libjpeg can read, or ran out. */
bool thiDecodeLuma (const uint8_t *jpeg, size_t len, int maxWidth,
                    std::vector<uint8_t> &luma, int &width, int &height);

/* Where frames come from: a camera, or a test's own. */
class thiSource
{
public:
    virtual ~thiSource () { }

    /* The next frame's JPEG and its capture time in seconds, waiting at
       most waitMs. False with status empty if none came in time, false
       with a status if the source is gone. */
    virtual bool next (std::vector<uint8_t> &jpeg, double &seconds, int waitMs,
                       std::string &status) = 0;
};

/* What a camera saw in one frame. */
struct thiAxes
{
    float  axis[thMotion::AXES];
    double seconds;
    bool   connected;

    thiAxes () : seconds(0), connected(false)
    {
        for (int i = 0; i < thMotion::AXES; i++)
            axis[i] = 0;
    }
};

/* Reads a camera on a thread of its own and hands what it sees to another
   thread, which drains it.

   connect opens a source, or says why it can't (status); it is asked
   again retryMs later. wake is called on the reader's thread when there is
   something to drain, once until the next drain. */
class thiCamera
{
public:
    typedef std::function<std::unique_ptr<thiSource> (std::string &status)> Connect;

    /* Frames kept for a drain that comes late; past this the oldest go. */
    static const size_t KEPT = 64;

    /* Frames are decoded toward this width; see thiDecodeLuma. */
    static const int WIDTH = 160;

    thiCamera (const Connect &connect, const std::function<void ()> &wake,
               int retryMs = 1000);
    ~thiCamera ();

    /* Every frame since the last drain, oldest first, so a burst in a zone
       is seen by a drainer slower than the camera; false if none. A camera
       that goes is heard as one frame with connected false and nothing
       moving. */
    bool drain (std::vector<thiAxes> &frames);

    /* Why there is no camera, or empty. */
    std::string status () const;

private:
    Connect                connect_;
    std::function<void ()> wake_;
    int                    retryMs_;
    std::atomic<bool>      stop_;
    std::atomic<bool>      notified_;
    mutable std::mutex     lock_;
    std::condition_variable stopped_;
    std::vector<thiAxes>   frames_;
    std::string            status_;
    std::thread            thread_;

    void run ();
    void publish (const thiAxes &a);
    void setStatus (const std::string &status);
    void rest (int ms);
};

#endif /* THI_CAMERA_H */
