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

#include "thhIndex.h"

const uint8_t THH_INDEX_INIT[2][5] = {
    { 0x04, 0x00, 0x00, 0x00, 0x00 },   /* change mode: start reporting */
    { 0x07, 0x03, 0x00, 0x00, 0x00 },   /* light-sensor verbosity */
};

/* In enum order, so a button's or an axis's index is its slot. */
const thhControls &
thhIndexControls ()
{
    static const thhControls c = {
        "index",
        { "a", "b", "system", "trigger_click", "thumbstick",
          "a_touch", "b_touch", "system_touch", "trigger_touch",
          "thumbstick_touch", "trackpad_touch" },
        {
            { "grip_top", false }, { "grip_mid", false },
            { "grip_low", false }, { "trigger_near", false },
            { "grip_force", false }, { "trigger", false },
            { "pad_x", true }, { "pad_y", true }, { "pad_force", false },
            { "stick_x", true }, { "stick_y", true },
        },
    };

    return c;
}

/* Where each button's bit is: byte, mask. */
static const uint8_t BITS[THH_INDEX_BUTTONS][2] = {
    { 0x08, 0x04 },     /* a */
    { 0x09, 0x10 },     /* b */
    { 0x09, 0x20 },     /* system */
    { 0x08, 0x01 },     /* trigger_click */
    { 0x0a, 0x04 },     /* thumbstick */
    { 0x15, 0x10 },     /* a_touch */
    { 0x15, 0x20 },     /* b_touch */
    { 0x15, 0x08 },     /* system_touch */
    { 0x15, 0x01 },     /* trigger_touch */
    { 0x15, 0x40 },     /* thumbstick_touch */
    { 0x0a, 0x10 },     /* trackpad_touch */
};

/* And each 8-bit axis's byte. */
static const uint8_t BYTES[][2] = {
    { THH_INDEX_GRIP_TOP,     0x16 },
    { THH_INDEX_GRIP_MID,     0x17 },
    { THH_INDEX_GRIP_LOW,     0x18 },
    { THH_INDEX_TRIGGER_NEAR, 0x19 },
    { THH_INDEX_GRIP_FORCE,   0x1a },
    { THH_INDEX_TRIGGER,      0x0c },
    { THH_INDEX_PAD_FORCE,    0x1b },
};

static float
signed16 (const uint8_t *p)
{
    return (float)(int16_t)(p[0] | (p[1] << 8)) / 32767;
}

bool
thhIndexFeed (const uint8_t *data, size_t len, thhState &s)
{
    /* 0x02-0x03 are the report's type: 0x3c07 is input, 0x0b04 a ping
       carrying the battery. */
    if (len < 0x1c || data[0] != 0x01 || data[2] != 0x07 || data[3] != 0x3c)
        return false;

    s.buttons = 0;
    for (int b = 0; b < THH_INDEX_BUTTONS; b++)
        if (data[BITS[b][0]] & BITS[b][1])
            s.buttons |= 1u << b;

    for (size_t i = 0; i < sizeof(BYTES) / sizeof(BYTES[0]); i++)
        s.axis[BYTES[i][0]] = data[BYTES[i][1]] / 255.0f;

    /* One x and y, the trackpad's while it is touched and the stick's
       otherwise; the one not reporting rests at the middle. */
    const bool pad = s.buttons & (1u << THH_INDEX_TRACKPAD_TOUCH);
    const float x = signed16(data + 0x10), y = signed16(data + 0x12);

    s.axis[THH_INDEX_PAD_X] = pad ? x : 0;
    s.axis[THH_INDEX_PAD_Y] = pad ? y : 0;
    s.axis[THH_INDEX_STICK_X] = pad ? 0 : x;
    s.axis[THH_INDEX_STICK_Y] = pad ? 0 : y;

    return true;
}
