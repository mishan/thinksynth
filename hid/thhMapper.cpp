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

#include <algorithm>
#include <cmath>
#include <cstring>

#include "thhMapper.h"

/* Values closer than this to the last one sent aren't sent: below what an
   8-bit control can resolve, so only noise is lost. */
#define THH_DEADBAND (1.0 / 256)

double
thhActionValue (const thhAction &a, double argMin, double argMax)
{
    const double lo = a.hasMin ? a.min : argMin;
    const double hi = a.hasMax ? a.max : argMax;

    if (a.exp && lo > 0 && hi > 0)
        return lo * std::pow(hi / lo, a.t);
    return lo + a.t * (hi - lo);
}

thhMapper::thhMapper ()
    : pitch_(-1), velocity_(-1), octave_(0)
{
    memset(held_, 0, sizeof(held_));
    memset(sounding_, 0, sizeof(sounding_));
    memset(sent_, 0, sizeof(sent_));
    memset(last_, 0, sizeof(last_));
}

bool
thhMapper::configure (const thcHidBlock &profile, const thhControls &controls,
                      std::vector<thhAction> &out, std::string &err,
                      int &errLine)
{
    std::vector<Bound> buttons, axes;
    int pitch = -1, velocity = -1;

    errLine = 1;
    if (profile.device != controls.device)
    {
        err = "this profile is for '" + profile.device + "', not '" +
              controls.device + "'";
        return false;
    }
    if (controls.buttons.size() > THH_MAX_BUTTONS ||
        controls.axes.size() > THH_MAX_AXES)
    {
        err = "the " + controls.device + " has more controls than a mapper "
              "holds";
        return false;
    }

    for (size_t i = 0; i < profile.bindings.size(); i++)
    {
        const thcHidBinding &b = profile.bindings[i];
        const bool wantsButton = b.kind == thcHidBinding::NOTE ||
                                 b.kind == thcHidBinding::HOLD ||
                                 b.kind == thcHidBinding::TOGGLE ||
                                 b.kind == thcHidBinding::OCTAVE;
        const int button = controls.button(b.control);
        const int axis = controls.axis(b.control);

        errLine = b.line;
        if (button < 0 && axis < 0)
        {
            err = "'" + b.control + "' is not a control the " +
                  controls.device + " has";
            return false;
        }
        if (wantsButton && button < 0)
        {
            err = "'" + b.control + "' is an axis; a note, a modifier or an "
                  "octave step wants a button";
            return false;
        }
        if (!wantsButton && axis < 0)
        {
            err = "'" + b.control + "' is a button; a value, a pitch axis or "
                  "a velocity source wants an axis";
            return false;
        }

        if (b.kind == thcHidBinding::PITCH || b.kind == thcHidBinding::VELOCITY)
        {
            int &slot = b.kind == thcHidBinding::PITCH ? pitch : velocity;

            if (slot >= 0)
            {
                err = b.kind == thcHidBinding::PITCH
                      ? "a profile has one pitch axis"
                      : "a profile has one velocity source";
                return false;
            }
            slot = (int)axes.size();
        }

        if (wantsButton)
            buttons.push_back({ b, button, false });
        else
            axes.push_back({ b, axis, controls.axes[axis].bipolar });
    }

    release(out);
    profile_ = profile;
    buttons_.swap(buttons);
    axes_.swap(axes);
    pitch_ = pitch;
    velocity_ = velocity;
    octave_ = 0;
    memset(sent_, 0, sizeof(sent_));

    return true;
}

/* An axis's position as 0..1, so a bipolar one rests in the middle. */
double
thhMapper::along (const Bound &ax, const thhState &s) const
{
    const double v = s.axis[ax.index];
    const double t = ax.bipolar ? (v + 1) / 2 : v;

    return t < 0 ? 0 : t > 1 ? 1 : t;
}

int
thhMapper::channelOf (const thcHidBinding &b, int curChannel) const
{
    return b.channel ? b.channel - 1
           : profile_.channel ? profile_.channel - 1
           : curChannel;
}

void
thhMapper::press (const Bound &bt, const thhState &s, int curChannel,
                  std::vector<thhAction> &out)
{
    const thcHidBinding &b = bt.b;

    if (b.kind == thcHidBinding::OCTAVE)
    {
        /* No further than a base note can go, so stepping back is heard at
           once. */
        const int lo = -(profile_.base / 12), hi = (127 - profile_.base) / 12;

        octave_ = std::max(lo, std::min(hi, octave_ + b.semis));
        return;
    }
    if (b.kind != thcHidBinding::NOTE)
        return;

    int note = profile_.base + 12 * octave_ + b.semis;

    if (pitch_ >= 0)
    {
        const thcHidBinding &p = axes_[pitch_].b;
        const double v = s.axis[axes_[pitch_].index];

        note += (int)std::lround(v * p.range) * (p.octaves ? 12 : 1);
    }

    if (note < 0 || note > 127)
        return;

    int vel = profile_.velocity;

    if (velocity_ >= 0)
    {
        const thcHidBinding &v = axes_[velocity_].b;
        const double lo = v.hasMin ? v.min : 1;
        const double hi = v.hasMax ? v.max : 127;

        vel = (int)std::lround(lo + along(axes_[velocity_], s) * (hi - lo));
    }

    const int channel = channelOf(b, curChannel);
    Held &h = held_[bt.index];

    h.on = true;
    h.channel = channel;
    h.note = note;

    if (sounding_[channel][note]++ == 0)
    {
        thhAction a = thhAction();

        a.kind = thhAction::NOTE_ON;
        a.channel = channel;
        a.note = note;
        a.vel = std::max(1, std::min(127, vel));
        out.push_back(a);
    }
}

void
thhMapper::lift (const Bound &bt, std::vector<thhAction> &out)
{
    Held &h = held_[bt.index];

    if (!h.on)
        return;
    h.on = false;

    if (--sounding_[h.channel][h.note] == 0)
    {
        thhAction a = thhAction();

        a.kind = thhAction::NOTE_OFF;
        a.channel = h.channel;
        a.note = h.note;
        out.push_back(a);
    }
}

void
thhMapper::update (const thhState &s, int curChannel,
                   std::vector<thhAction> &out)
{
    if (curChannel < 0 || curChannel > 15)
        curChannel = 0;

    if (!s.connected)
    {
        if (prev_.connected)
            release(out);
        prev_ = s;
        return;
    }

    /* Coming back, every button that is down is a press. */
    const uint32_t was = prev_.connected ? prev_.buttons : 0;

    for (size_t i = 0; i < buttons_.size(); i++)
    {
        const uint32_t bit = 1u << buttons_[i].index;

        if ((s.buttons & bit) && !(was & bit))
            press(buttons_[i], s, curChannel, out);
        else if (!(s.buttons & bit) && (was & bit))
            lift(buttons_[i], out);
    }

    for (size_t i = 0; i < axes_.size(); i++)
    {
        const Bound &ax = axes_[i];

        if (ax.b.kind != thcHidBinding::VALUE)
            continue;

        const double t = along(ax, s);

        /* The ends are always sent, or a slow move would stop short. */
        if (sent_[ax.index] && (t == last_[ax.index] ||
                                (std::fabs(t - last_[ax.index]) < THH_DEADBAND &&
                                 t > 0 && t < 1)))
            continue;
        sent_[ax.index] = true;
        last_[ax.index] = t;

        thhAction a = thhAction();

        a.kind = thhAction::VALUE;
        a.channel = channelOf(ax.b, curChannel);
        a.arg = ax.b.arg;
        a.t = t;
        a.hasMin = ax.b.hasMin;
        a.hasMax = ax.b.hasMax;
        a.min = ax.b.min;
        a.max = ax.b.max;
        a.exp = ax.b.shape == thcHidBinding::EXP;
        out.push_back(a);
    }

    prev_ = s;
}

void
thhMapper::release (std::vector<thhAction> &out)
{
    for (size_t i = 0; i < buttons_.size(); i++)
        lift(buttons_[i], out);
}
