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
    : buttons_(0), axes_(0), velocity_(100), octave_(0)
{
    memset(held_, 0, sizeof(held_));
    memset(heldAxis_, 0, sizeof(heldAxis_));
    memset(armed_, 0, sizeof(armed_));
    memset(below_, 0, sizeof(below_));
    memset(sounding_, 0, sizeof(sounding_));
    memset(sent_, 0, sizeof(sent_));
    memset(last_, 0, sizeof(last_));
    memset(lastChannel_, 0, sizeof(lastChannel_));
}

/* One layer's bindings onto the device's controls. */
bool
thhMapper::bindLayer (const std::vector<thcHidBinding> &bindings,
                      const thcHidBlock &profile, const thhControls &controls,
                      Layer &l, std::string &err, int &errLine)
{
    for (int i = 0; i < THH_MAX_BUTTONS; i++)
        l.button[i] = -1;
    for (int i = 0; i < THH_MAX_AXES; i++)
        l.axis[i] = -1;
    l.pitch = l.velocity = -1;

    for (size_t i = 0; i < bindings.size(); i++)
    {
        const thcHidBinding &b = bindings[i];
        const bool wantsButton = b.kind == thcHidBinding::NOTE ||
                                 b.kind == thcHidBinding::HOLD ||
                                 b.kind == thcHidBinding::TOGGLE ||
                                 b.kind == thcHidBinding::OCTAVE;
        const int button = controls.button(b.control);
        const int axis = controls.axis(b.control);
        Bound bound = { b, wantsButton ? button : axis, false, -1 };

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
            int &slot = b.kind == thcHidBinding::PITCH ? l.pitch : l.velocity;

            if (slot >= 0)
            {
                err = b.kind == thcHidBinding::PITCH
                      ? "a layer has one pitch axis"
                      : "a layer has one velocity source";
                return false;
            }
            slot = (int)l.binds.size();
        }

        /* The parser has checked the layer exists. */
        for (size_t j = 0; j < profile.layers.size(); j++)
            if (profile.layers[j].name == b.layer)
                bound.layer = (int)j + 1;

        if (!wantsButton)
            bound.bipolar = controls.axes[axis].bipolar;
        (wantsButton ? l.button : l.axis)[bound.index] = (int)l.binds.size();
        l.binds.push_back(bound);
    }

    return true;
}

bool
thhMapper::configure (const thcHidBlock &profile, const thhControls &controls,
                      std::vector<thhAction> &out, std::string &err,
                      int &errLine)
{
    std::vector<Layer> layers(profile.layers.size() + 1);

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

    layers[0].channel = profile.channel;
    layers[0].base = profile.base;
    if (!bindLayer(profile.bindings, profile, controls, layers[0], err,
                   errLine))
        return false;

    for (size_t i = 0; i < profile.layers.size(); i++)
    {
        const thcHidLayer &pl = profile.layers[i];
        Layer &l = layers[i + 1];

        l.name = pl.name;
        l.channel = pl.channel;
        l.base = pl.base;
        if (!bindLayer(pl.bindings, profile, controls, l, err, errLine))
            return false;

        /* Or the layer could take away the way out of itself. */
        for (size_t j = 0; j < l.binds.size(); j++)
        {
            const int at = layers[0].button[l.binds[j].index];
            const bool button = l.binds[j].b.kind == thcHidBinding::NOTE ||
                                l.binds[j].b.kind == thcHidBinding::OCTAVE;

            if (button && at >= 0 &&
                (layers[0].binds[at].b.kind == thcHidBinding::HOLD ||
                 layers[0].binds[at].b.kind == thcHidBinding::TOGGLE))
            {
                err = "'" + l.binds[j].b.control + "' switches a layer; a "
                      "layer can't bind it to anything else";
                errLine = l.binds[j].b.line;
                return false;
            }
        }
    }

    /* What stays on across an edit, in the order it went on: a toggle whose
       layer a button still toggles, and a hold whose button is still down
       and still holds a layer. */
    std::vector<Active> active;

    for (size_t i = 0; i < active_.size(); i++)
    {
        const Active &a = active_[i];

        for (size_t j = 0; j < layers[0].binds.size(); j++)
        {
            const Bound &m = layers[0].binds[j];
            const bool same = m.layer >= 0 &&
                              layers[m.layer].name == layers_[a.layer].name;

            if (a.toggle && same && m.b.kind == thcHidBinding::TOGGLE)
            {
                active.push_back({ m.layer, -1, true });
                break;
            }
            if (!a.toggle && m.index == a.button && prev_.connected &&
                (prev_.buttons & (1u << a.button)) &&
                m.b.kind == thcHidBinding::HOLD)
            {
                active.push_back({ m.layer, a.button, false });
                break;
            }
        }
    }

    release(out);
    layers_.swap(layers);
    active_.swap(active);
    buttons_ = (int)controls.buttons.size();
    axes_ = (int)controls.axes.size();
    velocity_ = profile.velocity;
    octave_ = 0;
    memset(sent_, 0, sizeof(sent_));
    memset(armed_, 0, sizeof(armed_));

    for (size_t i = 0; i < active_.size(); i++)
        if (!active_[i].toggle)
            held_[active_[i].button].what = Held::HOLD;

    return true;
}

/* What a control does now: the newest active layer that binds it, else the
   profile's own binding, else nothing. */
const thhMapper::Bound *
thhMapper::find (bool axis, int index) const
{
    for (size_t i = active_.size(); i-- > 0;)
    {
        const Layer &l = layers_[active_[i].layer];
        const int at = axis ? l.axis[index] : l.button[index];

        if (at >= 0)
            return &l.binds[at];
    }

    const int at = axis ? layers_[0].axis[index] : layers_[0].button[index];

    return at >= 0 ? &layers_[0].binds[at] : NULL;
}

/* The pitch axis or the velocity source: the newest active layer's that has
   one, else the profile's. */
const thhMapper::Bound *
thhMapper::source (bool pitch) const
{
    for (size_t i = active_.size() + 1; i-- > 0;)
    {
        const Layer &l = layers_[i ? active_[i - 1].layer : 0];
        const int at = pitch ? l.pitch : l.velocity;

        if (at >= 0)
            return &l.binds[at];
    }

    return NULL;
}

int
thhMapper::channelOf (const thcHidBinding &b, int curChannel) const
{
    if (b.channel)
        return b.channel - 1;
    for (size_t i = active_.size(); i-- > 0;)
        if (layers_[active_[i].layer].channel)
            return layers_[active_[i].layer].channel - 1;
    return layers_[0].channel ? layers_[0].channel - 1 : curChannel;
}

int
thhMapper::base () const
{
    for (size_t i = active_.size(); i-- > 0;)
        if (layers_[active_[i].layer].base >= 0)
            return layers_[active_[i].layer].base;
    return layers_[0].base;
}

/* An axis's position as 0..1, so a bipolar one rests in the middle. */
double
thhMapper::along (const Bound &ax, const thhState &s) const
{
    const double v = s.axis[ax.index];
    const double t = ax.bipolar ? (v + 1) / 2 : v;

    return t < 0 ? 0 : t > 1 ? 1 : t;
}

void
thhMapper::press (int button, const thhState &s, int curChannel,
                  std::vector<thhAction> &out)
{
    const Bound *bt = find(false, button);

    if (!bt)
        return;

    const thcHidBinding &b = bt->b;

    if (b.kind == thcHidBinding::OCTAVE)
    {
        /* No further than a base note can go, so stepping back is heard at
           once; and never against the step, if a layer's base has moved the
           range out from under it. */
        const int lo = -(base() / 12), hi = (127 - base()) / 12;
        const int to = std::max(lo, std::min(hi, octave_ + b.semis));

        octave_ = b.semis > 0 ? std::max(octave_, to) : std::min(octave_, to);
        return;
    }

    if (b.kind == thcHidBinding::HOLD)
    {
        active_.push_back({ bt->layer, button, false });
        held_[button].what = Held::HOLD;
        return;
    }

    if (b.kind == thcHidBinding::TOGGLE)
    {
        for (size_t i = 0; i < active_.size(); i++)
            if (active_[i].toggle && active_[i].layer == bt->layer)
            {
                active_.erase(active_.begin() + i);
                return;
            }
        active_.push_back({ bt->layer, button, true });
        return;
    }

    if (b.kind == thcHidBinding::NOTE)
        sound(b, held_[button], s, curChannel, -1, out);
}

void
thhMapper::sound (const thcHidBinding &b, Held &h, const thhState &s,
                  int curChannel, int vel, std::vector<thhAction> &out)
{
    const Bound *pitch = source(true);
    const Bound *velocity = source(false);
    int note = base() + 12 * octave_ + b.semis;

    if (pitch)
        note += (int)std::lround(s.axis[pitch->index] * pitch->b.range) *
                (pitch->b.octaves ? 12 : 1);

    if (note < 0 || note > 127)
        return;

    if (vel < 0 && velocity)
    {
        const double lo = velocity->b.hasMin ? velocity->b.min : 1;
        const double hi = velocity->b.hasMax ? velocity->b.max : 127;

        vel = (int)std::lround(lo + along(*velocity, s) * (hi - lo));
    }
    else if (vel < 0)
        vel = velocity_;

    const int channel = channelOf(b, curChannel);

    h.what = Held::NOTE;
    h.channel = channel;
    h.note = note;
    h.off = b.off;

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
thhMapper::unsound (Held &h, std::vector<thhAction> &out)
{
    if (h.what != Held::NOTE)
        return;
    h.what = Held::NONE;

    if (--sounding_[h.channel][h.note] == 0)
    {
        thhAction a = thhAction();

        a.kind = thhAction::NOTE_OFF;
        a.channel = h.channel;
        a.note = h.note;
        out.push_back(a);
    }
}

/* A strike's speed is how long the axis took from its off to its at: this
   quick or quicker is its loudest, this slow or slower its softest. */
#define THH_STRIKE_FAST 0.01
#define THH_STRIKE_SLOW 0.25

void
thhMapper::strike (int axis, const thhState &s, int curChannel,
                   std::vector<thhAction> &out)
{
    Held &h = heldAxis_[axis];
    const float v = s.axis[axis];

    /* Let go where it began, whatever binds the axis now. */
    if (h.what == Held::NOTE && v < h.off)
        unsound(h, out);

    const Bound *bd = find(true, axis);

    if (!bd || bd->b.kind != thcHidBinding::STRIKE)
    {
        armed_[axis] = false;
        return;
    }

    if (v < bd->b.off)
    {
        armed_[axis] = true;
        below_[axis] = s.time;
    }
    else if (v >= bd->b.at && armed_[axis] && h.what != Held::NOTE)
    {
        const double lo = bd->b.hasMin ? bd->b.min : 20;
        const double hi = bd->b.hasMax ? bd->b.max : 127;
        const double t = std::max(0.0, std::min(1.0,
            (s.time - below_[axis] - THH_STRIKE_FAST) /
            (THH_STRIKE_SLOW - THH_STRIKE_FAST)));

        armed_[axis] = false;
        sound(bd->b, h, s, curChannel, (int)std::lround(hi - t * (hi - lo)),
              out);
    }
}

void
thhMapper::lift (int button, std::vector<thhAction> &out)
{
    Held &h = held_[button];

    if (h.what != Held::HOLD)
    {
        unsound(h, out);
        return;
    }

    h.what = Held::NONE;
    for (size_t i = 0; i < active_.size(); i++)
        if (!active_[i].toggle && active_[i].button == button)
        {
            active_.erase(active_.begin() + i);
            break;
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

    for (int b = 0; b < buttons_; b++)
    {
        const uint32_t bit = 1u << b;

        if ((s.buttons & bit) && !(was & bit))
            press(b, s, curChannel, out);
        else if (!(s.buttons & bit) && (was & bit))
            lift(b, out);
    }

    for (int ax = 0; ax < axes_; ax++)
        strike(ax, s, curChannel, out);

    for (int ax = 0; ax < axes_; ax++)
    {
        const Bound *bd = find(true, ax);

        if (!bd || bd->b.kind != thcHidBinding::VALUE)
        {
            sent_[ax] = false;
            continue;
        }

        const double t = along(*bd, s);
        const int channel = channelOf(bd->b, curChannel);

        /* A new target hears where the control is at once. Otherwise the
           ends are always sent, or a slow move would stop short. */
        if (sent_[ax] && lastArg_[ax] == bd->b.arg &&
            lastChannel_[ax] == channel &&
            (t == last_[ax] ||
             (std::fabs(t - last_[ax]) < THH_DEADBAND && t > 0 && t < 1)))
            continue;
        sent_[ax] = true;
        last_[ax] = t;
        lastArg_[ax] = bd->b.arg;
        lastChannel_[ax] = channel;

        thhAction a = thhAction();

        a.kind = thhAction::VALUE;
        a.channel = channel;
        a.arg = bd->b.arg;
        a.t = t;
        a.hasMin = bd->b.hasMin;
        a.hasMax = bd->b.hasMax;
        a.min = bd->b.min;
        a.max = bd->b.max;
        a.exp = bd->b.shape == thcHidBinding::EXP;
        out.push_back(a);
    }

    prev_ = s;
}

/* Holds let go with the notes; toggles stay as they are. */
void
thhMapper::release (std::vector<thhAction> &out)
{
    for (int b = 0; b < THH_MAX_BUTTONS; b++)
        lift(b, out);
    for (int a = 0; a < THH_MAX_AXES; a++)
        unsound(heldAxis_[a], out);
}
