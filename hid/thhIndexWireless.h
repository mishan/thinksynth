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

#ifndef THH_INDEX_WIRELESS_H
#define THH_INDEX_WIRELESS_H

#include <stddef.h>
#include <stdint.h>

#include "thhIndex.h"

/* The Index controller through a Watchman dongle (28de:2101, 2102), from
   captures and the dongle as libsurvive and Monado read it.

   The dongle's input report 0x23 is one radio message and 0x24 two, each
   its time's high byte, its length, its time's low byte and a payload of
   length - 1 bytes; 0x26 says the controller has gone (turned off, or
   plugged in by its cable, which takes it off the radio).

   A controller wakes speaking what libsurvive calls Watchman v1 (Monado's
   decode_watchmanv2), whose messages can't be told apart by length; sent
   THH_INDEX_WIRELESS_V2 it speaks v2 until it goes, and only v2 is read.
   A v2 payload is a flags byte and what it says follows, in order:

     0x80  an IMU sample: the time's lowest byte, then the wired report's
           six int16 (the 48 MHz tick is high, low, that, then 0)
     0x40  metadata: 0x80 and the battery, 0x20 and a byte, 0x30 and two
     0x20  input: f0 a1 and the wired report's bytes 0x15..0x1b (touches,
           grip, trigger_near, squeeze, pad force); or f1..f7, whose 0x01
           is the buttons byte, 0x04 the trigger, 0x02 x and y, in that
           order
     0x10  light, the rest of the message, unread: nothing says how long
     0x01  set at times, with nothing that follows for it

   0x08, 0x04 and 0x02 are never set in v2, and its sections take the
   message's length exactly, or within it with light: of some 318,000
   messages captured, every v2 one is read that way and no v1 one is.

   Input is sent only as it changes, so the state carries from message to
   message: a button or touch is down until a message says it isn't. */

/* The v2 switch, a feature report on the dongle, sent padded to its 64
   bytes as SteamVR's lighthouse_console sends it. */
extern const uint8_t THH_INDEX_WIRELESS_V2[6];

/* And the dongle's pairing, sent as the switch is: for the next 30 seconds
   (0x7530 ms) it pairs with a controller put in its own pairing mode, by
   holding System and B from off, and keeps it from then on. */
extern const uint8_t THH_INDEX_WIRELESS_PAIR[6];

struct thhIndexWireless
{
    thhIndexImu imu;
    unsigned    refused;        /* messages not v2 since the last that was */

    thhIndexWireless () : refused(0) {}
};

/* A dongle report into s, which carries what earlier ones said. True if
   any of its messages was v2, and then s is as they say. False, s as it
   was, for a report none of whose messages is; but after a second of
   those in a row, as from a controller back in v1 before it was switched,
   and for 0x26, s.connected goes false and true is returned, for its
   reader to let go and connect again. */
bool thhIndexFeedWireless (const uint8_t *data, size_t len,
                           thhIndexWireless &w, thhState &s);

#endif /* THH_INDEX_WIRELESS_H */
