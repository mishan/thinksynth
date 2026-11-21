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

#include <map>
#include <set>
#include <sstream>
#include <string>

#include <emscripten.h>

#include "thhHost.h"
#include "thhMapper.h"
#include "thhPhone.h"
#include "thhProfile.h"

static thhHost host;

/* A phone (hid/thhPhone.h, docs/PHONE.md): its frames as its page sent
   them over the link, the mapper its profile plays through, and what that
   has played since the page last asked. On the keyboard's channel, as the
   Index plays. */
static thhPhone phone;
static thhMapper phoneMapper;
static std::vector<thhAction> phonePlayed;
static int phoneChannel = 0;
/* Between a pairing's start, th_phone_profile, and its end: a frame still
   on its way after the end plays nothing. */
static bool phoneOn = false;

/* What the last call that returns text said, kept until the next. */
static std::string said;

extern "C" {

/* What a controller's configuration, inflated, says it is (thhIndexConfig):
   its hand, a newline, and its serial number, either empty. */
EMSCRIPTEN_KEEPALIVE const char *th_config (const char *json)
{
    std::string hand, serial;

    thhIndexConfig(json, hand, serial);
    said = hand + "\n" + serial;
    return said.c_str();
}

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
    phoneChannel = channel;
}

/* A pairing started, or the player's profile changed: the phone's own
   profile with the player's `text' over it. What is wrong with the
   player's, or nothing; what fits plays either way. */
EMSCRIPTEN_KEEPALIVE const char *th_phone_profile (const char *text)
{
    thHidBlock profile;
    std::string err, configureErr;
    int line = 0;

    if (!phoneOn)
        phone = thhPhone();
    phoneOn = true;
    thhLoadProfileText(text, "your profile", thhPhoneControls(),
                       THH_PHONE_PROFILE, "", profile, err);
    if (!phoneMapper.configure(profile, thhPhoneControls(), phonePlayed,
                               configureErr, line))
        err += (err.empty() ? "" : "; ") + configureErr;
    said = err;
    return said.c_str();
}

/* A frame from the phone's page: its page load's id, its count, its 12
   axes and the fingers down. Whether it was taken. */
EMSCRIPTEN_KEEPALIVE int th_phone_frame (uint32_t id, uint32_t seq,
                                         const float *axes, uint32_t down,
                                         double now)
{
    thhPhoneFrame f = { id, seq, {}, down };

    if (!phoneOn)
        return 0;
    for (int a = 0; a < THH_PHONE_AXES; a++)
        f.axes[a] = axes[a];

    /* A quiet second with no tick in it is still the phone gone: its notes
       let go before this one plays. Only then, since an update of the
       state it is still in would leave its smoothing no time to move. */
    const thhState was = phone.state(now);

    if (!was.connected)
        phoneMapper.update(was, phoneChannel, phonePlayed);

    const bool taken = phone.take(f, now);

    phoneMapper.update(phone.state(now), phoneChannel, phonePlayed);
    return taken;
}

/* The phone between frames, which lets everything go once it has been
   silent too long: whether it is there. */
EMSCRIPTEN_KEEPALIVE int th_phone_tick (double now)
{
    /* Between pairings, the last frame of the one that ended is nobody's. */
    if (!phoneOn)
        return 0;

    const thhState s = phone.state(now);

    phoneMapper.update(s, phoneChannel, phonePlayed);
    return s.connected;
}

/* Everything the phone holds let go: the pairing over. The mapper is told
   it has gone, so the next pairing starts with nothing held. */
EMSCRIPTEN_KEEPALIVE void th_phone_release (void)
{
    phoneMapper.release(phonePlayed);
    phoneMapper.update(thhState(), phoneChannel, phonePlayed);
    phoneOn = false;
}

/* What the phone has played since last asked, as thhActionsJson. */
EMSCRIPTEN_KEEPALIVE const char *th_phone_take (void)
{
    said = thhActionsJson(phonePlayed);
    phonePlayed.clear();
    return said.c_str();
}

EMSCRIPTEN_KEEPALIVE const char *th_profile (const char *text)
{
    said = host.profile(text);
    return said.c_str();
}

/* args: the patch's arg names, a line each. */
EMSCRIPTEN_KEEPALIVE const char *th_patch (const char *block, const char *name,
                                           const char *args)
{
    std::set<std::string> has;
    std::istringstream in(args);
    std::string one;

    while (std::getline(in, one))
        has.insert(one);
    said = host.patch(block, name, has);
    return said.c_str();
}

/* What the controller pane shows (thhHost::pane), as JSON. */
EMSCRIPTEN_KEEPALIVE const char *th_pane (void)
{
    said = host.pane();
    return said.c_str();
}

EMSCRIPTEN_KEEPALIVE void th_swap (void)
{
    host.session().swapHands();
}

EMSCRIPTEN_KEEPALIVE void th_clear_loops (void)
{
    host.session().clearLoops();
}

/* The hands swapped before, and as they are now: SERIAL=hand, a line
   each, as the page keeps them. */
EMSCRIPTEN_KEEPALIVE void th_remembered (const char *text)
{
    std::map<std::string, std::string> hands;
    std::istringstream in(text);
    std::string one;

    while (std::getline(in, one))
        if (one.find('=') != std::string::npos)
            hands[one.substr(0, one.find('='))] = one.substr(one.find('=') + 1);
    host.remembered(hands);
}

EMSCRIPTEN_KEEPALIVE const char *th_kept (void)
{
    said.clear();
    for (const auto &k : host.kept())
        said += k.first + "=" + k.second + "\n";
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
