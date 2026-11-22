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
#include <string.h>

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include <emscripten.h>

#include "thMotion.h"
#include "thhCamera.h"
#include "thhForm.h"
#include "thhHandTrack.h"
#include "thhHost.h"
#include "thhMapper.h"
#include "thhPhone.h"
#include "thhProfile.h"
#include "thhProfileEdit.h"

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

static std::string
num (double v)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

template <class T, class F>
static std::string
jsonList (const std::vector<T> &items, F each)
{
    std::string out = "[";

    for (const T &i : items)
        out += (out.size() > 1 ? "," : "") + each(i);
    return out + "]";
}

static std::string
str (const std::string &s)
{
    return thhJsonString(s);
}

static std::string
bindingJson (const thHidBinding &b)
{
    const char *scale = b.scale.empty() ? "" : thHidScaleName(b.scale);

    return "{\"kind\":" + str(thhFormKindName(b.kind)) +
           ",\"text\":" + str(thhBindingText(b)) +
           ",\"semis\":" + std::to_string(b.semis) +
           ",\"layer\":" + str(b.layer) + ",\"arg\":" + str(b.arg) +
           ",\"channel\":" + std::to_string(b.channel) +
           ",\"min\":" + (b.hasMin ? num(b.min) : "null") +
           ",\"max\":" + (b.hasMax ? num(b.max) : "null") +
           ",\"at\":" + num(b.at) + ",\"off\":" + num(b.off) +
           ",\"center\":" + num(b.center) + ",\"smooth\":" + num(b.smooth) +
           ",\"latch\":" + (b.latch ? "true" : "false") +
           ",\"both\":" + (b.both ? "true" : "false") +
           ",\"shape\":" + (b.shape == thHidBinding::EXP ? "\"exp\"" : "\"linear\"") +
           ",\"step\":" + (b.octaves ? "\"octave\"" : "\"semitone\"") +
           ",\"range\":" + std::to_string(b.range) +
           ",\"scale\":" + str(scale ? scale : "") + "}";
}

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

/* A device the configurator's form is for, by its name: its controls and
   its own profile, the hands' unsnapped as "hands:free". False for one it
   isn't. */
static bool
formDevice (const char *name, const thhControls *&controls, const char *&own)
{
    static const struct
    {
        const char *name;
        const thhControls &(*controls) ();
        const char *const *own;
    } DEVICES[] = {
        { "phone", thhPhoneControls, &THH_PHONE_PROFILE },
        { "index", thhIndexControls, &THH_INDEX_PROFILE },
        { "camera", thhCameraControls, &THH_CAMERA_PROFILE },
        { "hands", thhHandsControls, &THH_HANDS_PROFILE },
        { "hands:free", thhHandsControls, &THH_HANDS_FREE_PROFILE },
    };

    for (const auto &d : DEVICES)
        if (strcmp(name, d.name) == 0)
        {
            controls = &d.controls();
            own = *d.own;
            return true;
        }
    return false;
}

/* The configurator's form for `device' (phone or index) with the player's
   `text' over its own (thhForm.h), and the Index's patch's block over
   that, as JSON: what each kind of action takes, the scales, the player's
   layers, how the device's form is laid out, and a row per control, with
   the card it is on and what it is there. */
EMSCRIPTEN_KEEPALIVE const char *th_form (const char *device, const char *text)
{
    static const std::vector<std::string> OPTIONS = {
        "channel", "min", "max", "at", "off", "step", "scale", "range",
        "center", "smooth", "shape", "both", "latch" };
    const thhControls *controls;
    const char *own;

    if (!formDevice(device, controls, own))
        return "";

    const thhForm f = thhFormOf(*controls, own, text,
                                controls == &thhIndexControls() &&
                                host.session().patchPlays() ? host.session().patchText() : "");

    said = "{\"problem\":" + str(f.problem) +
        ",\"layers\":" + jsonList(f.layers, str) +
        ",\"scales\":" + jsonList(thHidScaleNames(), str) +
        ",\"kinds\":" + jsonList(thhFormKinds(), [](const thhFormKind &k) {
            std::vector<std::string> takes;

            for (const std::string &o : OPTIONS)
                if (thhTakes(k.kind, o))
                    takes.push_back(o);
            return "{\"name\":" + str(k.name) +
                   ",\"button\":" + (k.button ? "true" : "false") +
                   ",\"axis\":" + (k.axis ? "true" : "false") +
                   ",\"param\":" + str(k.param) +
                   ",\"takes\":" + jsonList(takes, str) + "}";
        }) +
        ",\"own\":" + str(f.own) +
        ",\"cannot\":" + jsonList(f.cannot, str) +
        ",\"backUp\":" + (f.backUp ? "true" : "false") +
        ",\"controls\":" + jsonList(f.controls, [](const thhFormControl &c) {
            return "{\"control\":" + str(c.control) +
                   ",\"card\":" + str(c.card) + ",\"role\":" + str(c.role) +
                   ",\"axis\":" + (c.axis ? "true" : "false") +
                   ",\"yours\":" + (c.yours ? "true" : "false") +
                   ",\"off\":" + (c.off ? "true" : "false") +
                   ",\"patched\":" + (c.patched ? "true" : "false") +
                   ",\"elsewhere\":" + jsonList(c.elsewhere, str) +
                   ",\"bindings\":" + jsonList(c.bindings, bindingJson) + "}";
        }) + "}";
    return said.c_str();
}

/* An edit of `device''s form (thhFormEdit) made to `text': {"text"} as it
   is now, or {"err"} saying why it wasn't made. */
EMSCRIPTEN_KEEPALIVE const char *th_form_edit (const char *device, const char *text,
                                               const char *op, const char *control,
                                               int which, const char *fields)
{
    const thhControls *controls;
    const char *own;
    std::string t = text, err = "there is no form for '" + std::string(device) + "'";

    said = formDevice(device, controls, own) &&
           thhFormEdit(t, *controls, own, op, control, which, fields, err)
        ? "{\"text\":" + str(t) + "}" : "{\"err\":" + str(err) + "}";
    return said.c_str();
}

/* Each of `device''s controls' value now, in the form's order, 0 or 1 for
   a button; or none while none is there. Of a pair of Index controllers,
   whichever is further from rest. */
EMSCRIPTEN_KEEPALIVE const char *th_form_values (const char *device, double now)
{
    const thhControls *controls;
    const char *own;
    std::vector<thhState> states;
    std::vector<double> v;

    if (!formDevice(device, controls, own))
        return "[]";
    if (controls == &thhPhoneControls() && phoneOn)
        states.push_back(phone.state(now));
    for (int i = 0; controls == &thhIndexControls() && i < host.session().controllers(); i++)
        states.push_back(host.session().stateOf(i));
    for (const thhState &s : states)
    {
        if (!s.connected)
            continue;
        if (v.empty())
            v.assign(controls->buttons.size() + controls->axes.size(), 0);
        for (size_t i = 0; i < v.size(); i++)
        {
            const size_t a = i - controls->buttons.size();
            const double x = i < controls->buttons.size() ? (s.buttons >> i) & 1
                                                          : s.axis[a];

            if (std::fabs(x) > std::fabs(v[i]))
                v[i] = x;
        }
    }
    said = jsonList(v, num);
    return said.c_str();
}

/* A camera's frames read as the air harp reads them (thMotion), for a
   phone that plays it and sends what it reads: where to write a width x
   height frame's luma, a byte a pixel, and then its axes, as JSON. */
static std::unique_ptr<thMotion> motion;
static std::vector<uint8_t> luma;

EMSCRIPTEN_KEEPALIVE uint8_t *th_motion_luma (int width, int height)
{
    luma.resize((size_t)width * height);
    return luma.data();
}

EMSCRIPTEN_KEEPALIVE const char *th_motion_feed (int width, int height, double seconds)
{
    std::vector<double> axes;

    if (width >= 3 && height >= 3 && luma.size() == (size_t)width * height)
    {
        if (!motion || motion->width() != width || motion->height() != height)
            motion.reset(new thMotion(width, height));
        motion->feed(luma.data(), width, seconds);
        for (int a = 0; a < thMotion::AXES; a++)
            axes.push_back(motion->axis((thMotion::Axis)a));
    }
    said = jsonList(axes, num);
    return said.c_str();
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
