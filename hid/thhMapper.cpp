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

/* How far under its values a posture's axes go before it lets go, so a
   hand at the edge of one doesn't flicker in and out of it: this, or half
   the way to the axis's bottom where that's nearer, or a posture low on
   its axis could never let go at all. */
#define THH_POSTURE_GIVE 0.15

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
    : buttons_(0), axes_(0), velocity_(100), octave_(0), bend_(0)
{
    memset(aux_, 0, sizeof(aux_));
    memset(held_, 0, sizeof(held_));
    memset(heldAxis_, 0, sizeof(heldAxis_));
    memset(armed_, 0, sizeof(armed_));
    memset(below_, 0, sizeof(below_));
    memset(sounding_, 0, sizeof(sounding_));
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
        l.axis[i].clear();
    l.velocity = -1;

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

        if (b.kind == thcHidBinding::VELOCITY)
        {
            if (l.velocity >= 0)
            {
                err = "a layer has one velocity source";
                return false;
            }
            l.velocity = (int)l.binds.size();
        }

        for (size_t j = 0; j < profile.layers.size(); j++)
            if (profile.layers[j].name == b.layer)
                bound.layer = (int)j + 1;
        if ((b.kind == thcHidBinding::HOLD || b.kind == thcHidBinding::TOGGLE) &&
            bound.layer < 0)
        {
            err = "there is no layer '" + b.layer + "'";
            return false;
        }

        if (!wantsButton)
            bound.bipolar = controls.axes[axis].bipolar;
        if (wantsButton)
            l.button[bound.index] = (int)l.binds.size();
        else
            l.axis[bound.index].push_back((int)l.binds.size());
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

    errLine = profile.line ? profile.line : 1;
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

        for (size_t j = 0; j < pl.when.size(); j++)
        {
            const std::string &w = pl.when[j].first;
            const int axis = controls.axis(w);

            errLine = pl.whenLine;
            if (axis < 0)
            {
                err = controls.button(w) >= 0
                      ? "'" + w + "' is a button; a posture wants axes"
                      : "'" + w + "' is not a control the " + controls.device +
                        " has";
                return false;
            }
            const double at = pl.when[j].second;
            const double floor = controls.axes[axis].bipolar ? -1 : 0;

            l.when.push_back(std::make_pair(axis, at));
            l.off.push_back(at - std::min(THH_POSTURE_GIVE, (at - floor) / 2));
        }
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
    bend_ = 0;
    memset(aux_, 0, sizeof(aux_));
    smoothing_.clear();
    sharedBend_ = 0;
    memset(sharedAux_, 0, sizeof(sharedAux_));
    memset(reached_, 0, sizeof(reached_));
    reachedList_.clear();
    for (int i = 0; i < THH_MAX_AXES; i++)
        sent_[i] = Sent();
    memset(armed_, 0, sizeof(armed_));

    for (size_t i = 0; i < active_.size(); i++)
        if (!active_[i].toggle)
            held_[active_[i].button].what = Held::HOLD;

    return true;
}

/* Which layer an axis answers to now: the newest active one that binds
   it, else the profile's own, else none. */
const thhMapper::Layer *
thhMapper::axisLayer (int axis) const
{
    for (size_t i = active_.size(); i-- > 0;)
        if (!layers_[active_[i].layer].axis[axis].empty())
            return &layers_[active_[i].layer];

    return layers_[0].axis[axis].empty() ? NULL : &layers_[0];
}

/* What a control does now, or the first of what an axis does. */
const thhMapper::Bound *
thhMapper::find (bool axis, int index) const
{
    if (axis)
    {
        const Layer *l = axisLayer(index);

        return l ? &l->binds[l->axis[index][0]] : NULL;
    }

    for (size_t i = active_.size(); i-- > 0;)
    {
        const Layer &l = layers_[active_[i].layer];

        if (l.button[index] >= 0)
            return &l.binds[l.button[index]];
    }

    const int at = layers_[0].button[index];

    return at >= 0 ? &layers_[0].binds[at] : NULL;
}

/* The velocity source: the newest active layer's that has one, else the
   profile's. */
const thhMapper::Bound *
thhMapper::velocitySource () const
{
    for (size_t i = active_.size() + 1; i-- > 0;)
    {
        const Layer &l = layers_[i ? active_[i - 1].layer : 0];

        if (l.velocity >= 0)
            return &l.binds[l.velocity];
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
    const double v = s.axis[ax.index] - ax.b.center;
    const double t = ax.bipolar ? (v + 1) / 2 : v;

    return t < 0 ? 0 : t > 1 ? 1 : t;
}

void
thhMapper::press (int button, const thhState &s, int curChannel,
                  std::vector<thhAction> &out)
{
    /* A latched note's next press ends it, whatever binds the button now. */
    if (held_[button].what == Held::NOTE && held_[button].latched)
    {
        unsound(held_[button], out);
        return;
    }

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
    {
        sound(b, held_[button], s, curChannel, -1, out);
        held_[button].latched = b.latch;
    }
}

void
thhMapper::sound (const thcHidBinding &b, Held &h, const thhState &s,
                  int curChannel, int vel, std::vector<thhAction> &out)
{
    const Bound *velocity = velocitySource();
    int note = base() + 12 * octave_ + b.semis;

    /* Every pitch axis in effect adds its steps. */
    for (int ax = 0; ax < axes_; ax++)
    {
        const Layer *l = axisLayer(ax);

        for (size_t i = 0; l && i < l->axis[ax].size(); i++)
        {
            const thcHidBinding &p = l->binds[l->axis[ax][i]].b;

            if (p.kind == thcHidBinding::PITCH)
                note += (int)std::lround((s.axis[ax] - p.center) * p.range) *
                        (p.octaves ? 12 : 1);
        }
    }

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
    h.latched = false;

    if (sounding_[channel][note]++ == 0)
    {
        thhAction a = thhAction();

        soundingList_.push_back(std::make_pair(channel, note));
        a.kind = thhAction::NOTE_ON;
        a.channel = channel;
        a.note = note;
        a.vel = std::max(1, std::min(127, vel));
        out.push_back(a);

        /* A note starts where the hand's expression is, not at rest. */
        if (bend_)
            express(thhAction::NOTE_BEND, 0, bend_, channel, note, out);
        for (int n = 0; n < 4; n++)
            if (aux_[n])
                express(thhAction::NOTE_AUX, n, aux_[n], channel, note, out);
    }
}

void
thhMapper::express (thhAction::Kind kind, int aux, double value, int channel,
                    int note, std::vector<thhAction> &out) const
{
    thhAction a = thhAction();

    a.kind = kind;
    a.channel = channel;
    a.note = note;
    a.aux = aux;
    a.t = value;
    out.push_back(a);
}

/* A share of the way from where a binding's smoothing was toward raw, the
   more the longer since; or raw, for a binding not smoothed. */
double
thhMapper::smoothed (int key, double raw, double k, const thhState &s)
{
    auto it = smoothing_.find(key);

    /* Close enough is there: a share of what is left, in floating point,
       comes to nothing just short of it. */
    if (k > 0 && it != smoothing_.end() &&
        std::fabs(raw - it->second.first) >= THH_DEADBAND)
        raw = it->second.first + (1 - std::exp(-(s.time - it->second.second) / k)) *
                                 (raw - it->second.first);
    smoothing_[key] = std::make_pair(raw, s.time);
    return raw;
}

/* Changes in the bend and the aux values, sent to every note this mapper is
   holding: each once, however many buttons hold it. */
void
thhMapper::expression (const thhState &s, std::vector<thhAction> &out)
{
    double bend = 0, aux[4], sharedBend = 0, sharedAux[4];
    bool shares[4] = { false, false, false, false };

    memcpy(aux, aux_, sizeof(aux));
    memcpy(sharedAux, sharedAux_, sizeof(sharedAux));
    for (int ax = 0; ax < axes_; ax++)
    {
        const Layer *l = axisLayer(ax);

        for (size_t i = 0; l && i < l->axis[ax].size(); i++)
        {
            const Bound &bd = l->binds[l->axis[ax][i]];
            /* By layer too: another layer's binding at the same place on
               the axis is another binding. */
            const int key = ((int)(l - &layers_[0]) * THH_MAX_AXES + ax) * 64 +
                            (int)i;

            if (bd.b.kind == thcHidBinding::BEND)
            {
                const double v = smoothed(key, (s.axis[ax] - bd.b.center) *
                                               bd.b.range, bd.b.smooth, s);

                bend += v;
                sharedBend += bd.b.both ? v : 0;
            }
            else if (bd.b.kind == thcHidBinding::AUX)
            {
                const double lo = bd.b.hasMin ? bd.b.min : 0;
                const double hi = bd.b.hasMax ? bd.b.max : 1;
                const int n = bd.b.semis;

                aux[n] = smoothed(key, lo + along(bd, s) * (hi - lo),
                                  bd.b.smooth, s);
                if (bd.b.both)
                {
                    sharedAux[n] = aux[n];
                    shares[n] = true;
                }
            }
        }
    }

    const bool bent = std::fabs(bend - bend_) >= 0.005;
    bool moved[4];

    for (int n = 0; n < 4; n++)
        moved[n] = std::fabs(aux[n] - aux_[n]) >= THH_DEADBAND;

    bend_ = bent ? bend : bend_;
    for (int n = 0; n < 4; n++)
        aux_[n] = moved[n] ? aux[n] : aux_[n];

    for (const auto &held : soundingList_)
    {
        if (bent)
            express(thhAction::NOTE_BEND, 0, bend_, held.first, held.second,
                    out);
        for (int n = 0; n < 4; n++)
            if (moved[n])
                express(thhAction::NOTE_AUX, n, aux_[n], held.first,
                        held.second, out);
    }

    /* The other hand's notes hear only this hand's shares: when a share
       moves, and, a note the other hand has just started, at once. */
    const bool sharedBent = std::fabs(sharedBend - sharedBend_) >= 0.005;
    bool sharedMoved[4];

    for (int n = 0; n < 4; n++)
        sharedMoved[n] = shares[n] &&
                         std::fabs(sharedAux[n] - sharedAux_[n]) >= THH_DEADBAND;
    sharedBend_ = sharedBent ? sharedBend : sharedBend_;
    for (int n = 0; n < 4; n++)
        sharedAux_[n] = sharedMoved[n] ? sharedAux[n] : sharedAux_[n];

    std::vector<std::pair<int, int> > theirs;

    for (const auto &held : other_ ? other_->soundingList_
                                   : std::vector<std::pair<int, int> >())
        if (!sounding_[held.first][held.second])
            theirs.push_back(held);

    for (const auto &was : reachedList_)
        reached_[was.first][was.second] = false;
    for (const auto &note : theirs)
    {
        const int ch = note.first, n0 = note.second;
        const bool fresh = std::find(reachedList_.begin(), reachedList_.end(),
                                     note) == reachedList_.end();

        reached_[ch][n0] = true;
        if (sharedBent || (fresh && sharedBend_))
            express(thhAction::NOTE_BEND, 0, sharedBend_, ch, n0, out);
        for (int n = 0; n < 4; n++)
            if (shares[n] && (sharedMoved[n] || (fresh && sharedAux_[n])))
                express(thhAction::NOTE_AUX, n, sharedAux_[n], ch, n0, out);
    }
    reachedList_.swap(theirs);
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

        soundingList_.erase(std::find(soundingList_.begin(), soundingList_.end(),
                                      std::make_pair(h.channel, h.note)));
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
    /* At off counts as under it, or a strike whose off is 0, an axis that
       rests at 0, would never let go. */
    if (h.what == Held::NOTE && v <= h.off)
        unsound(h, out);

    const Bound *bd = find(true, axis);

    if (!bd || bd->b.kind != thcHidBinding::STRIKE)
    {
        armed_[axis] = false;
        return;
    }

    if (v <= bd->b.off)
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

    if (h.what == Held::NOTE && h.latched)
        return;
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

    postures(s);

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

    expression(s, out);

    for (int ax = 0; ax < axes_; ax++)
    {
        const Layer *l = axisLayer(ax);
        std::vector<const Bound *> values;
        Sent to;

        for (size_t i = 0; l && i < l->axis[ax].size(); i++)
        {
            const Bound &bd = l->binds[l->axis[ax][i]];

            if (bd.b.kind == thcHidBinding::VALUE)
            {
                values.push_back(&bd);
                to.to.push_back(std::make_pair(channelOf(bd.b, curChannel),
                                               bd.b.arg));
                to.t.push_back(along(bd, s));
            }
        }

        /* A new set of targets hears where the axis is at once. Otherwise
           each is sent past the deadband, and the ends always, or a slow
           move would stop short. */
        const bool fresh = to.to != sent_[ax].to;

        /* A smoothed knob goes a share of the way there, the more the
           longer since the last state, and starts where the axis is. */
        to.time = s.time;
        to.smoothed = to.t;
        for (size_t i = 0; i < values.size(); i++)
        {
            const double k = values[i]->b.smooth;

            if (!fresh && k > 0 &&
                std::fabs(to.t[i] - sent_[ax].smoothed[i]) >= THH_DEADBAND)
            {
                const double prev = sent_[ax].smoothed[i];

                to.smoothed[i] = prev + (1 - std::exp(-(s.time - sent_[ax].time) / k)) *
                                        (to.t[i] - prev);
            }
            to.t[i] = to.smoothed[i];
        }

        for (size_t i = 0; i < values.size(); i++)
        {
            const double t = to.t[i];
            const Bound &bd = *values[i];

            if (!fresh && (t == sent_[ax].t[i] ||
                           (std::fabs(t - sent_[ax].t[i]) < THH_DEADBAND &&
                            t > 0 && t < 1)))
            {
                to.t[i] = sent_[ax].t[i];
                continue;
            }

            thhAction a = thhAction();

            a.kind = thhAction::VALUE;
            a.channel = to.to[i].first;
            a.arg = bd.b.arg;
            a.t = t;
            a.hasMin = bd.b.hasMin;
            a.hasMax = bd.b.hasMax;
            a.min = bd.b.min;
            a.max = bd.b.max;
            a.exp = bd.b.shape == thcHidBinding::EXP;
            out.push_back(a);
        }
        sent_[ax] = to;
    }

    prev_ = s;
}


void
thhMapper::postures (const thhState &s)
{
    for (size_t j = 1; j < layers_.size(); j++)
    {
        const Layer &l = layers_[j];
        bool all = !l.when.empty(), gone = false;
        size_t at = active_.size();

        for (size_t i = 0; i < active_.size(); i++)
            if (active_[i].posture && active_[i].layer == (int)j)
                at = i;

        for (size_t i = 0; i < l.when.size(); i++)
        {
            const float v = s.axis[l.when[i].first];

            all = all && v >= l.when[i].second;
            gone = gone || v < l.off[i];
        }

        if (at == active_.size() && all)
            active_.push_back({ (int)j, -1, false, true });
        else if (at < active_.size() && gone)
            active_.erase(active_.begin() + at);
    }
}

/* Holds and postures let go with the notes, the postures to be taken up
   again from the next state; toggles stay as they are. */
void
thhMapper::release (std::vector<thhAction> &out)
{
    for (int b = 0; b < THH_MAX_BUTTONS; b++)
    {
        held_[b].latched = false;
        lift(b, out);
    }
    for (int a = 0; a < THH_MAX_AXES; a++)
        unsound(heldAxis_[a], out);
    memset(armed_, 0, sizeof(armed_));

    /* This hand's share of the other hand's notes, which play on, taken
       back off them. */
    for (const auto &note : reachedList_)
    {
        if (sharedBend_)
            express(thhAction::NOTE_BEND, 0, 0, note.first, note.second, out);
        for (int n = 0; n < 4; n++)
            if (sharedAux_[n])
                express(thhAction::NOTE_AUX, n, 0, note.first, note.second, out);
    }
    sharedBend_ = 0;
    memset(sharedAux_, 0, sizeof(sharedAux_));
    memset(reached_, 0, sizeof(reached_));
    reachedList_.clear();
    for (size_t i = active_.size(); i-- > 0;)
        if (active_[i].posture)
            active_.erase(active_.begin() + i);
}
