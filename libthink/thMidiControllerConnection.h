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

#ifndef TH_MIDICONTROLLERCONNECTION_H
#define TH_MIDICONTROLLERCONNECTION_H 1

#include "thExport.h"

/* What a MIDI Map binding sets, as data: the chanarg by name on its
   destination channel, the range it moves over and whether exponentially.
   thSynth::handleMidiController moves it with setChanArgFraction, as a
   knob on the web page or a HID profile moves one. */

class THINK_API thMidiControllerConnection {
public:
    thMidiControllerConnection (float min, float max, int scale, int chan,
                                int controller, int dchan, string argName);
    ~thMidiControllerConnection (void);


    enum { LINEAR = 0, EXPONENTIAL };

    void setMin (float min) { min_ = min; }
    void setMax (float max) { max_ = max; }
    float min (void) { return min_; }
    float max (void) { return max_; }

    void setScale (int scale) { scale_ = scale; }
    int scale (void) { return scale_; }

    int chan (void) { return chan_; }
    int controller (void) { return controller_; }
    int destChan (void) { return destchan_; }
    string argName (void) { return argName_; }
private:
    int chan_, controller_;
    int destchan_;
    string argName_;
    float min_, max_;
    int scale_;
};

#endif /* TH_MIDICONTROLLERCONNECTION */
