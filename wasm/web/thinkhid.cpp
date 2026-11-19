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
 */

/*
 * thinkhid.cpp -- Valve Index controllers in the browser, decoded and
 * played as the desktop plays them: a host (hid/thhHost.h) the page's
 * hid.js feeds each WebHID input report, and asks what to play.
 *
 * A module of its own, not the worklet's: WebHID's reports arrive on the
 * page, and decoding them is microseconds, where the worklet's thread is
 * the audio's. Times are the page's, in seconds.
 */

#include <stdint.h>

#include <string>

#include <emscripten.h>

#include "thhHost.h"

static thhHost host;

/* What the last call that returns text said, kept until the next. */
static std::string said;

extern "C" {

/* A controller connected, wired or once a dongle's answers; its id. */
EMSCRIPTEN_KEEPALIVE int th_add (const char *hand, const char *serial,
                                 double now)
{
    return host.add(hand, serial, now);
}

/* from: 0 a wired controller's input, 1 its IMU, 2 a dongle. 1 if the
   dongle says the controller has gone. */
EMSCRIPTEN_KEEPALIVE int th_report (int id, int from, const uint8_t *data,
                                    int len, double now)
{
    return host.report(id, (thhHost::From)from, data, len, now);
}

EMSCRIPTEN_KEEPALIVE void th_drop (int id, double now)
{
    host.drop(id, now);
}

EMSCRIPTEN_KEEPALIVE void th_channel (int channel)
{
    host.channel(channel);
}

EMSCRIPTEN_KEEPALIVE const char *th_profile (const char *text)
{
    said = host.profile(text);
    return said.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *th_patch (const char *block, const char *name)
{
    said = host.patch(block, name);
    return said.c_str();
}

EMSCRIPTEN_KEEPALIVE int th_tick (double now)
{
    return host.tick(now);
}

EMSCRIPTEN_KEEPALIVE void th_release (double now)
{
    host.release(now);
}

EMSCRIPTEN_KEEPALIVE const char *th_take (void)
{
    said = host.take();
    return said.c_str();
}

}
