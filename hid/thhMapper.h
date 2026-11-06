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

#ifndef THH_MAPPER_H
#define THH_MAPPER_H

#include <string>
#include <vector>

#include "thcHidBlock.h"
#include "thhControls.h"

/* What a profile asks a host to do. Patch mode turns these into synth
   calls; a chain's input turns them into composer events. */
struct thhAction
{
    enum Kind { NOTE_ON, NOTE_OFF, VALUE };

    Kind        kind;
    int         channel;        /* 0-15 */
    int         note, vel;      /* NOTE_*; vel is 1-127 */
    std::string arg;            /* VALUE */
    double      t;              /* VALUE: 0..1 along the control */
    bool        hasMin, hasMax;
    double      min, max;       /* VALUE: the profile's range, where given */
    bool        exp;
};

/* An action's value in the arg's units: the profile's range where it gave
   one, the arg's own where it didn't. */
double thhActionValue (const thhAction &a, double argMin, double argMax);

class thhMapper
{
public:
    thhMapper ();

    /* False, with what and where, if the profile names a control the device
       doesn't have or binds one to an action it can't do; the mapper then
       plays on as it was. Otherwise the notes the old profile held end, in
       out, and buttons still down don't sound until pressed again; layers
       still toggled, or held by a button still down, stay on. */
    bool configure (const thcHidBlock &profile, const thhControls &controls,
                    std::vector<thhAction> &out, std::string &err,
                    int &errLine);

    /* curChannel (0-15) is the host's, for bindings that name none. */
    void update (const thhState &s, int curChannel,
                 std::vector<thhAction> &out);

    /* Ends every note this mapper is holding and lets go of every hold: an
       unplug, an edit, a handover. Toggles stay on. Buttons still down don't
       sound again until pressed again. */
    void release (std::vector<thhAction> &out);

private:
    struct Bound
    {
        thcHidBinding b;
        int           index;        /* the device's button or axis */
        bool          bipolar;
        int           layer;        /* HOLD, TOGGLE: into layers_ */
    };

    /* Layer 0 is the profile's own bindings, always under the rest. */
    struct Layer
    {
        std::string        name;
        int                channel;     /* 1-16; 0 inherits */
        int                base;        /* -1 inherits */
        std::vector<Bound> binds;
        int                button[THH_MAX_BUTTONS], axis[THH_MAX_AXES];
        int                pitch, velocity;     /* into binds, or -1 */
    };

    /* A layer switched on, by the button holding it or by a toggle; the
       newest is last and wins. */
    struct Active
    {
        int  layer, button;
        bool toggle;
    };

    /* What a button's press did, so its release can undo exactly that. */
    struct Held
    {
        enum What { NONE, NOTE, HOLD } what;
        int    channel, note;
        double off;             /* a strike's: where it lets go */
    };

    std::vector<Layer>  layers_;
    std::vector<Active> active_;
    int                 buttons_, axes_, velocity_;

    thhState    prev_;
    Held        held_[THH_MAX_BUTTONS];
    int         sounding_[16][128];         /* buttons holding each note */
    int         octave_;
    bool        sent_[THH_MAX_AXES];
    double      last_[THH_MAX_AXES];
    std::string lastArg_[THH_MAX_AXES];
    int         lastChannel_[THH_MAX_AXES];

    static bool bindLayer (const std::vector<thcHidBinding> &bindings,
                           const thcHidBlock &profile,
                           const thhControls &controls, Layer &l,
                           std::string &err, int &errLine);
    const Bound *find (bool axis, int index) const;
    const Bound *source (bool pitch) const;
    int channelOf (const thcHidBinding &b, int curChannel) const;
    int base () const;
    /* Strikes, by axis: the note each holds, and whether the axis has been
       under its off since, and when it last was. */
    Held   heldAxis_[THH_MAX_AXES];
    bool   armed_[THH_MAX_AXES];
    double below_[THH_MAX_AXES];

    void press (int button, const thhState &s, int curChannel,
                std::vector<thhAction> &out);
    void lift (int button, std::vector<thhAction> &out);
    void strike (int axis, const thhState &s, int curChannel,
                 std::vector<thhAction> &out);
    /* vel < 0 plays at the velocity source's, or the profile's. */
    void sound (const thcHidBinding &b, Held &h, const thhState &s,
                int curChannel, int vel, std::vector<thhAction> &out);
    void unsound (Held &h, std::vector<thhAction> &out);
    double along (const Bound &ax, const thhState &s) const;
};

#endif /* THH_MAPPER_H */
