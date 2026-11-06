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

#include <cmath>

#include "thhIndex.h"

const uint8_t THH_INDEX_INIT[2][5] = {
    { 0x04, 0x00, 0x00, 0x00, 0x00 },   /* change mode: start reporting */
    { 0x07, 0x03, 0x00, 0x00, 0x00 },   /* light-sensor verbosity */
};

/* The controller's own instrument, after The Hands and the MI.MU gloves:
   the thumb picks a degree on the trackpad and the trigger strikes it,
   louder the faster it is pulled; tilt picks the octave, from the angle a
   controller is held at; the grip's squeeze is loudness and brightness at
   once, and the fingers' zones and the wrist shape the sound as it plays.
   Its knobs are dsp/index.dsp's, and are dropped on a patch without them,
   so the notes play anything. */
const char *const THH_INDEX_PROFILE =
    "device index {\n"
    "    base = \"C4\";\n"
    "\n"
    "    trigger = strike 0 { at = 0.5; };\n"
    "    a = note 0;\n"
    "    b = note 7;\n"
    "    pad_y = pitch { range = 7; };\n"
    "    tilt = pitch { step = octave; range = 1; center = 0.25; };\n"
    "\n"
    "    grip_force = \"level\" { min = 0.6; max = 1; };\n"
    "    grip_force = \"bright\" { min = 0.3; max = 0.9; };\n"
    "    grip_mid = \"growl\";\n"
    "    grip_low = \"air\" { max = 0.5; };\n"
    "    stick_y = \"wobble\" { min = -1; max = 1; };\n"
    "    roll = \"pan\" { min = -1; max = 1; };\n"
    "\n"
    "    layer fist {\n"
    "        when { grip_top = 0.85; grip_mid = 0.85; grip_low = 0.85; };\n"
    "        roll = \"bend\" { min = -1; max = 1; };\n"
    "    };\n"
    "};\n";

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
            { "tilt", true }, { "roll", true }, { "twist", true },
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

/* Walks the descriptor's short items (HID 1.11, 6.2.2.2) for the globals
   that size and number a report and the Input items that declare one:
   the bits of input with no report ID, whether any report has an ID, and
   whether one of the inputs is report 0x20. False if it is cut short. */
static bool
walk (const uint8_t *desc, size_t len, unsigned &bits, bool &ids, bool &imu)
{
    unsigned size = 0, count = 0, id = 0;

    bits = 0;
    ids = imu = false;
    for (size_t i = 0; i < len;)
    {
        const uint8_t prefix = desc[i];
        const size_t n = (prefix & 3) == 3 ? 4 : prefix & 3;
        unsigned v = 0;

        if (prefix == 0xfe)             /* a long item: skip it whole */
        {
            if (i + 1 >= len)
                return false;
            i += 3 + desc[i + 1];
            continue;
        }
        if (i + 1 + n > len)
            return false;
        for (size_t k = 0; k < n; k++)
            v |= (unsigned)desc[i + 1 + k] << (8 * k);
        i += 1 + n;

        switch (prefix & 0xfc)
        {
        case 0x84: id = v; ids = true; break;   /* Report ID */
        case 0x74: size = v; break;             /* Report Size */
        case 0x94: count = v; break;            /* Report Count */
        case 0x80:                              /* Input */
            if (!ids)
                bits += size * count;
            imu = imu || id == 0x20;
            break;
        }
    }

    return true;
}

bool
thhIndexIsInput (const uint8_t *desc, size_t len)
{
    unsigned bits;
    bool ids, imu;

    return walk(desc, len, bits, ids, imu) && !ids && bits == 64 * 8;
}

bool
thhIndexIsImu (const uint8_t *desc, size_t len)
{
    unsigned bits;
    bool ids, imu;

    return walk(desc, len, bits, ids, imu) && imu;
}

/* How much of each sample the smoothed gravity takes: at about 750 samples
   a second, a time constant near 70 ms, steady enough to hold a pose
   without lagging a turn of the wrist much. Twist, already a rate, is only
   taken off its noise. */
#define THH_INDEX_GRAVITY 0.02f
#define THH_INDEX_TWISTING 0.2f
/* The gyro's count at a quick twist of the wrist, which reads as 1. */
#define THH_INDEX_TWIST_FULL 8192.0f

static float
clamp1 (float v)
{
    return v < -1 ? -1 : v > 1 ? 1 : v;
}

bool
thhIndexFeedImu (const uint8_t *data, size_t len, thhIndexImu &imu,
                 thhState &s)
{
    if (len < 52 || data[0] != 0x20)
        return false;

    for (int k = 0; k < 3; k++)
    {
        const uint8_t *p = data + 1 + 17 * k;
        float a[3];

        for (int i = 0; i < 3; i++)
            a[i] = (int16_t)(p[2 * i] | (p[2 * i + 1] << 8));

        const float twist = (int16_t)(p[6] | (p[7] << 8)) /
                            THH_INDEX_TWIST_FULL;

        for (int i = 0; i < 3; i++)
            imu.g[i] = imu.primed ? imu.g[i] + THH_INDEX_GRAVITY * (a[i] - imu.g[i])
                                  : a[i];
        imu.twist = imu.primed ? imu.twist + THH_INDEX_TWISTING *
                                     (twist - imu.twist)
                               : twist;
        imu.primed = true;
    }

    /* The front is -x, and the top -y in the hand. */
    const float g = std::sqrt(imu.g[0] * imu.g[0] + imu.g[1] * imu.g[1] +
                              imu.g[2] * imu.g[2]);

    if (g > 0)
    {
        s.axis[THH_INDEX_TILT] = clamp1(std::asin(clamp1(-imu.g[0] / g)) /
                                        (float)(M_PI / 2));
        s.axis[THH_INDEX_ROLL] = clamp1(std::atan2(imu.g[2], -imu.g[1]) /
                                        (float)(M_PI / 2));
    }
    s.axis[THH_INDEX_TWIST] = clamp1(imu.twist);

    return true;
}
