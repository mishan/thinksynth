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

#ifndef THH_INDEX_H
#define THH_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "thhControls.h"

/* The Valve Index controller, wired (28de:2300), read from its input
   interface. Report 0x01 carries the buttons, touches, trigger, trackpad,
   stick and the grip's capacitive zones; the IMU and the light sensors are
   on interfaces of their own and aren't read. The offsets come from
   captures, pinned by scripts/hidcheck.cpp, not from documentation.

   The grip's zones are named for what they measure, not for fingers: the
   ring and little fingers share the lowest. On the stick and the trackpad,
   +y is away from the hand. */

enum thhIndexButton
{
    THH_INDEX_A, THH_INDEX_B, THH_INDEX_SYSTEM, THH_INDEX_TRIGGER_CLICK,
    THH_INDEX_THUMBSTICK,
    THH_INDEX_A_TOUCH, THH_INDEX_B_TOUCH, THH_INDEX_SYSTEM_TOUCH,
    THH_INDEX_TRIGGER_TOUCH, THH_INDEX_THUMBSTICK_TOUCH,
    THH_INDEX_TRACKPAD_TOUCH,
    THH_INDEX_BUTTONS
};

enum thhIndexAxis
{
    THH_INDEX_GRIP_TOP, THH_INDEX_GRIP_MID, THH_INDEX_GRIP_LOW,
    THH_INDEX_TRIGGER_NEAR, THH_INDEX_GRIP_FORCE, THH_INDEX_TRIGGER,
    THH_INDEX_PAD_X, THH_INDEX_PAD_Y, THH_INDEX_PAD_FORCE,
    THH_INDEX_STICK_X, THH_INDEX_STICK_Y,
    THH_INDEX_AXES
};

const thhControls &thhIndexControls ();

/* The two feature reports that wake every interface; until they are sent
   the controller says nothing. */
extern const uint8_t THH_INDEX_INIT[2][5];

/* An input report into s. False, leaving s as it was, for anything that
   isn't one: a ping, another report, too few bytes. */
bool thhIndexFeed (const uint8_t *data, size_t len, thhState &s);

#endif /* THH_INDEX_H */
