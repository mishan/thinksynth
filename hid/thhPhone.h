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

#ifndef THH_PHONE_H
#define THH_PHONE_H

#include <cstdint>

#include "thhControls.h"

/* A phone as a controller: the device `phone' (docs/PHONE.md). Its page
   sends the whole state in every frame, already as the player holds it:
   tilt in the screen's own axes, and each finger in the slot it took when
   it landed. What is here is what the host makes of a frame: whether to
   take it, and what the phone is doing now. */

enum thhPhoneButton
{
    THH_PHONE_TOUCH_1,
    THH_PHONE_TOUCH_2,
    THH_PHONE_TOUCH_3,
    THH_PHONE_TOUCH_4,
    THH_PHONE_TOUCH_5,
    THH_PHONE_BUTTONS
};

/* Tilt bipolar, right and up as the screen is held; a finger's x across
   and y up the screen, 0 to 1. */
enum thhPhoneAxis
{
    THH_PHONE_TILT_X,
    THH_PHONE_TILT_Y,
    THH_PHONE_TOUCH_1_X,
    THH_PHONE_TOUCH_1_Y,
    THH_PHONE_AXES = THH_PHONE_TOUCH_1_X + 2 * THH_PHONE_BUTTONS
};

const thhControls &thhPhoneControls ();

/* A theremin under one finger, for dsp/theremin.dsp as the hands' is:
   across is the scale, up is loud (aux3, the hush), a second finger an
   octave up and a third one down, and tilting right brightens (aux1). */
extern const char *const THH_PHONE_PROFILE;

/* A frame as it came: `id' the phone page's, random per load, and `seq'
   counting within it. */
struct thhPhoneFrame
{
    uint32_t id, seq;
    float    axes[THH_PHONE_AXES];
    uint32_t down;              /* a bit per finger */
};

class thhPhone
{
public:
    /* Four heartbeats: one lost is nothing, and a phone asleep says so
       itself when it can, with a frame of nothing down. */
    static constexpr double GONE_SECONDS = 1.0;

    /* A frame come at `seconds': taken if it is well formed and later
       than the last of its id. Any other id is a page loaded since, whose
       frames replace the last one's: there is one phone on a pairing. */
    bool take (const thhPhoneFrame &f, double seconds);

    /* The phone at `seconds': connected until nothing has come for
       GONE_SECONDS, when everything it held is let go. */
    thhState state (double seconds) const;

    /* Frames not taken: malformed, or no later than the last, as the
       second copy of a frame sent on both channels is. */
    unsigned dropped () const { return dropped_; }

private:
    bool          have_ = false;
    thhPhoneFrame last_ = {};
    double        at_ = 0;
    unsigned      dropped_ = 0;
};

#endif /* THH_PHONE_H */
