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

#ifndef THH_SESSION_H
#define THH_SESSION_H

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "thcHidBlock.h"
#include "thhControls.h"
#include "thhLooper.h"
#include "thhMapper.h"

/* Index controllers, one or a pair, played as the desktop's gthHid and the
   browser's page play them, without a toolkit, a synth or a device: what
   each slot's controller did comes in, and what to play goes out.

   One controller alone plays the profile's either-hand block, whichever
   hand it is; a pair play it each with its own hand's block on top, and
   what one bends `of = both' reaches the other's notes. */
class thhSession
{
public:
    /* sound plays the actions: notes, values, bends; now is the clock
       loops are timed on, in seconds; remember is told a pair's hands
       swapped, serial to hand, to keep for next time. */
    thhSession (const std::function<void (const std::vector<thhAction> &)> &sound,
                const std::function<double ()> &now,
                const std::function<void (const std::map<std::string,
                                                   std::string> &)> &remember);
    ~thhSession ();

    /* A slot for a controller found, by its serial number (empty if it
       has none), and one gone. */
    int add (const std::string &serial);
    void remove (int i);

    /* What slot i's controller did since last: each state at which a
       button or the connection changed, then the latest; said, the hand
       it says it is, or empty; channel, the keyboard's, 0-based, for
       bindings that name none. */
    void update (int i, const std::vector<thhState> &edges,
                 const thhState &latest, const std::string &said,
                 int channel);

    /* The profiles: the player's text (empty for none) as name, and the
       patch's controller block as patchName's. False, with the file, line
       and what is wrong in err, if either doesn't fit; what fits plays. */
    bool load (const std::string &player, const std::string &name,
               const std::string &patch, const std::string &patchName,
               std::string &err);

    /* The patch's controller block as last loaded, and its patch's name;
       and another patch with the same block named, which plays on as it
       was. */
    const std::string &patchText () const { return patchText_; }
    const std::string &patchName () const { return patchName_; }
    void rename (const std::string &patchName);

    /* The channel the player's profile sets, 0-based, or -1 for the
       keyboard's. */
    int playChannel () const { return playChannel_; }

    /* The hands each slot was given before, by serial number. */
    void remembered (const std::map<std::string, std::string> &hands)
    {
        remembered_ = hands;
    }

    /* Plays what the loops have due; true while any is playing, which is
       when the host is to call it, every few milliseconds. */
    bool tick ();
    bool looping () const { return looper_.loops() > 0; }

    /* Every note the controllers hold let go of, and the loops stopped. */
    void release ();

    int controllers () const { return (int)slots_.size(); }
    std::string handOf (int i) const;
    const thcHidBlock &profileOf (int i) const;
    thhState stateOf (int i) const { return slots_[i].latest; }
    unsigned generation () const { return generation_; }
    bool canSwapHands () const;
    void swapHands ();
    std::string loops () const;
    void clearLoops ();
    std::string patch () const;

private:
    struct Slot
    {
        std::string serial, hand, said;
        thhState    latest;
    };

    std::function<void (const std::vector<thhAction> &)> sound_;
    std::function<double ()> now_;
    std::function<void (const std::map<std::string, std::string> &)> remember_;
    unsigned generation_;
    bool     pair_;

    /* What either hand plays alone, and each in a pair; and a mapper for
       each hand. */
    thcHidBlock profiles_[3];
    thhMapper   mappers_[2];

    std::map<std::string, std::string> remembered_;

    /* Each hand's bend of each note, which add. */
    std::map<std::pair<int, int>, double> bends_[2];

    std::string patchText_, patchName_, patchError_;
    int         playChannel_ = -1;     /* -1: the keyboard's */

    /* What both hands play, looped. */
    thhLooper looper_;
    thhVoices voices_;

    std::vector<Slot> slots_;

    static int side (const std::string &hand) { return hand == "left" ? 0 : 1; }
    void hands ();
    void configure (bool pair);
    /* at: when the hand played them, on now's clock. */
    void play (int hand, const std::vector<thhAction> &acts, double at = 0);
    void sounded (const std::vector<thhAction> &acts);
};

#endif /* THH_SESSION_H */
