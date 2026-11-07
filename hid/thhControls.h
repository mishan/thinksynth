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

#ifndef THH_CONTROLS_H
#define THH_CONTROLS_H

#include <stdint.h>

#include <string>
#include <vector>

#define THH_MAX_AXES    16
#define THH_MAX_BUTTONS 32

/* A controller at one moment, already decoded. Axes are 0..1, or -1..1
   where the device calls the axis bipolar. */
struct thhState
{
    float    axis[THH_MAX_AXES];
    uint32_t buttons;           /* bit i: the device's button i is down */
    bool     connected;
    double   time;              /* seconds, on a clock that only goes on */

    thhState () : buttons(0), connected(false), time(0)
    {
        for (int i = 0; i < THH_MAX_AXES; i++)
            axis[i] = 0;
    }
};

/* What a kind of device has, by the names a profile uses. A button's or an
   axis's index is its position here, and its bit or slot in thhState. */
struct thhControls
{
    struct Axis
    {
        std::string name;
        bool        bipolar;
    };

    std::string              device;
    std::vector<std::string> buttons;
    std::vector<Axis>        axes;

    int button (const std::string &name) const;   /* -1 if it has none */
    int axis (const std::string &name) const;
};

#endif /* THH_CONTROLS_H */
