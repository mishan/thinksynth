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
       out, and buttons still down don't sound until pressed again. */
    bool configure (const thcHidBlock &profile, const thhControls &controls,
                    std::vector<thhAction> &out, std::string &err,
                    int &errLine);

    /* curChannel (0-15) is the host's, for bindings that name none. */
    void update (const thhState &s, int curChannel,
                 std::vector<thhAction> &out);

    /* Ends every note this mapper is holding: an unplug, an edit, a
       handover. Buttons still down don't sound again until pressed again. */
    void release (std::vector<thhAction> &out);

private:
    struct Bound
    {
        thcHidBinding b;
        int           index;        /* the device's button or axis */
        bool          bipolar;
    };

    struct Held
    {
        bool on;
        int  channel, note;
    };

    thcHidBlock        profile_;
    std::vector<Bound> buttons_, axes_;
    int                pitch_, velocity_;   /* into axes_, or -1 */

    thhState prev_;
    Held     held_[THH_MAX_BUTTONS];
    int      sounding_[16][128];            /* buttons holding each note */
    int      octave_;
    bool     sent_[THH_MAX_AXES];
    double   last_[THH_MAX_AXES];

    void press (const Bound &bt, const thhState &s, int curChannel,
                std::vector<thhAction> &out);
    void lift (const Bound &bt, std::vector<thhAction> &out);
    double along (const Bound &ax, const thhState &s) const;
    int channelOf (const thcHidBinding &b, int curChannel) const;
};

#endif /* THH_MAPPER_H */
