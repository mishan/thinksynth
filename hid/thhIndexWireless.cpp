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

#include "thhIndexWireless.h"

const uint8_t THH_INDEX_WIRELESS_V2[6] = { 0xff, 0x87, 0x03, 0x00, 0x01, 0x00 };
const uint8_t THH_INDEX_WIRELESS_PAIR[6] = { 0xff, 0xad, 0x03, 0x01, 0x30, 0x75 };

/* How many messages not v2 in a row mean a controller in v1: a second of
   its IMU's. */
#define THH_INDEX_WIRELESS_REFUSED 250

/* The buttons byte's bits. */
static const uint8_t BUTTONS[][2] = {
    { THH_INDEX_TRIGGER_CLICK,  0x01 },
    { THH_INDEX_TRACKPAD_TOUCH, 0x02 },
    { THH_INDEX_THUMBSTICK,     0x04 },
    { THH_INDEX_SYSTEM,         0x08 },
    { THH_INDEX_A,              0x10 },
    { THH_INDEX_B,              0x20 },
};

/* And the touch byte's, as the wired report's 0x15. */
static const uint8_t TOUCHES[][2] = {
    { THH_INDEX_TRIGGER_TOUCH,    0x01 },
    { THH_INDEX_SYSTEM_TOUCH,     0x08 },
    { THH_INDEX_A_TOUCH,          0x10 },
    { THH_INDEX_B_TOUCH,          0x20 },
    { THH_INDEX_THUMBSTICK_TOUCH, 0x40 },
};

/* The axes after the touch byte, in order. */
static const int GRIP[] = {
    THH_INDEX_GRIP_TOP, THH_INDEX_GRIP_MID, THH_INDEX_GRIP_LOW,
    THH_INDEX_TRIGGER_NEAR, THH_INDEX_GRIP_FORCE, THH_INDEX_PAD_FORCE,
};

static void
bits (const uint8_t (*map)[2], size_t n, uint8_t byte, thhState &s)
{
    for (size_t i = 0; i < n; i++)
        if (byte & map[i][1])
            s.buttons |= 1u << map[i][0];
        else
            s.buttons &= ~(1u << map[i][0]);
}

/* One message into imu and s, which are left as they were unless all of
   it is v2. */
static bool
message (const uint8_t *m, thhIndexImu &imu, thhState &s)
{
    const size_t n = m[1];

    if (n < 2 || n > 27)
        return false;

    const uint8_t *p = m + 3, *end = p + n - 1;
    const uint8_t flags = *p++;
    thhIndexImu i2 = imu;
    thhState s2 = s;

    if (flags & 0x0e)
        return false;

    if (flags & 0x80)
    {
        if (end - p < 13)
            return false;

        const uint32_t tick = ((uint32_t)m[0] << 24) | (m[2] << 16) |
                              (p[0] << 8);

        /* A time it has already had would read to the sample code as a
           new start. */
        if (!i2.primed || tick != i2.tick)
        {
            thhIndexImuSample(i2, p + 1, tick);
            thhIndexImuAxes(i2, s2);
        }
        p += 13;
    }

    if (flags & 0x40)
    {
        const size_t len = end - p < 1 ? 0 : *p == 0x30 ? 3 :
                           *p == 0x80 || *p == 0x20 ? 2 : 0;

        if (len == 0 || end - p < (ptrdiff_t)len)
            return false;
        p += len;
    }

    if (flags & 0x20)
    {
        if (end - p < 1)
            return false;

        const uint8_t head = *p++;

        if (head == 0xf0)
        {
            if (end - p < 8 || p[0] != 0xa1)
                return false;
            bits(TOUCHES, sizeof(TOUCHES) / sizeof(TOUCHES[0]), p[1], s2);
            for (size_t k = 0; k < sizeof(GRIP) / sizeof(GRIP[0]); k++)
                s2.axis[GRIP[k]] = p[2 + k] / 255.0f;
            p += 8;
        }
        else if (head > 0xf0 && head <= 0xf7)
        {
            const ptrdiff_t need = (head & 1) + (head >> 2 & 1) +
                                   4 * (head >> 1 & 1);

            if (end - p < need)
                return false;
            if (head & 0x01)
                bits(BUTTONS, sizeof(BUTTONS) / sizeof(BUTTONS[0]), *p++, s2);
            if (head & 0x04)
                s2.axis[THH_INDEX_TRIGGER] = *p++ / 255.0f;
            if (head & 0x02)
            {
                thhIndexXY(p, s2);
                p += 4;
            }
        }
        else
            return false;
    }

    if (p != end && !(flags & 0x10))
        return false;

    imu = i2;
    s = s2;
    return true;
}

bool
thhIndexFeedWireless (const uint8_t *data, size_t len, thhIndexWireless &w,
                      thhState &s)
{
    if (len >= 1 && data[0] == 0x26)
    {
        s.connected = false;
        return true;
    }

    const size_t count = len >= 1 && data[0] == 0x23 ? 1 :
                         len >= 1 && data[0] == 0x24 ? 2 : 0;
    bool read = false;

    if (count == 0 || len < 1 + 29 * count)
        return false;

    for (size_t k = 0; k < count; k++)
        if (message(data + 1 + 29 * k, w.imu, s))
        {
            w.refused = 0;
            read = true;
        }
        else if (++w.refused >= THH_INDEX_WIRELESS_REFUSED)
        {
            w.refused = 0;
            s.connected = false;
            return true;
        }

    return read;
}
