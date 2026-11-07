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


#ifndef THH_INDEX_HIDRAW_H
#define THH_INDEX_HIDRAW_H

#include <string>
#include <vector>

#include "thhReader.h"

/* Linux only: a wired Index controller found in sysfs and opened by hidraw.

   The /dev nodes of one controller: the input interface's, the IMU's if it
   has one, and all of them, every one of which is woken. */
struct thhIndexNodes
{
    std::string              usb;       /* the USB device's sysfs path */
    std::string              serial;    /* its USB serial number */
    std::string              input, imu;
    std::vector<std::string> all;
};

/* The first wired Index controller under sysRoot (normally /sys), matched by
   vendor, product and name, since a 2018 Vive Tracker has the same ID. Its
   nodes are named under devRoot (normally /dev). */
bool thhFindIndex (const std::string &sysRoot, const std::string &devRoot,
                   thhIndexNodes &out);

/* Every wired Index controller, in the same order every time. */
std::vector<thhIndexNodes> thhFindIndexes (const std::string &sysRoot,
                                           const std::string &devRoot);

/* Which hand a controller is shaped for, from the configuration it keeps
   (zlib-compressed JSON, whose model_number is "Knuckles Left" or
   "Knuckles Right"): "left", "right", or empty if it doesn't say. */
std::string thhIndexHand (const std::string &compressedConfig);

/* What makes the Index's reports a state, for a connection whose read is
   its input's fd and then its IMU's, if it has one. */
thhFeed thhIndexFeeder ();

/* The connect for a real controller, the one at usb (a path
   thhFindIndexes gave) or, if usb is empty, the first: each node opened
   and sent the controller's wake-up, and its hand read from its
   configuration. */
bool thhConnectIndex (const std::string &sysRoot, const std::string &devRoot,
                      const std::string &usb, thhConnection &c,
                      std::string &status);

#endif /* THH_INDEX_HIDRAW_H */
