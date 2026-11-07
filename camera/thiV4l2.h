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

#ifndef THI_V4L2_H
#define THI_V4L2_H

#include <memory>
#include <string>

#include "thiCamera.h"

/* A V4L2 camera giving Motion-JPEG: `device', or if that is empty the
   first /dev/video node that captures MJPEG. 640x480 at the fastest rate
   the camera has for it: a webcam does 30 there and some do 120, and at
   120 a frame is in hand 8 ms after its exposure starts where at 30 it is
   33.

   While open, the camera's exposure_dynamic_framerate is off: on, a dim
   room stretches exposure and a camera asked for 120 fps gives 15. It is
   put back as it was when the source closes.

   Null, with status saying why, if there is no such camera. */
std::unique_ptr<thiSource> thiOpenV4l2 (const std::string &device, std::string &status);

#endif /* THI_V4L2_H */
