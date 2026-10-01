/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#ifndef THC_MIDIEXPORT_H
#define THC_MIDIEXPORT_H 1

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "thcMidiFile.h"

class thSynth;
class thcPlugin;
class thcScheduler;

/* A piece, composed offline into a Standard MIDI File: what genwav
 * --midi, the Composer's Export MIDI and the page's do.
 *
 * describe() is what a thcMidiFile is told about the piece before a note
 * reaches it -- its name, the chains' names, what each channel holds, the
 * sections round the arrangement, the chanargs' declared ranges -- shared
 * by genwav, which renders audio alongside, and render() below.
 *
 * render() loads the piece onto a scheduler of its own over `synth', a
 * synth the caller made for it and made silent (thSynth::setSilent) --
 * nothing is heard and the host's own synth is not touched -- and steps
 * it as fast as it goes: to the end of an arrangement that ends
 * (`section end;'), and otherwise for `seconds'. The same seed composes
 * the same piece, so an export from a host playing with a seed is what
 * the host played.
 */
namespace thcMidiExport
{
    struct Options
    {
        double seconds = 120;           /* a piece that does not end    */
        bool   fine = false;            /* 14-bit controllers            */
        double seed = -1;               /* below zero: the piece's own,
                                           or drawn                      */
    };

    void describe (thcMidiFile &midi, thcScheduler &sched, thSynth &synth,
                   const std::string &name, double seconds);

    /* False, with `why', where the piece did not load. `length' is how
       long was composed. */
    bool render (const std::map<std::string, thcPlugin *> &plugins,
                 thSynth *synth, const std::string &genPath,
                 const Options &options, std::vector<uint8_t> &out,
                 std::string &why, double *length = NULL);
}

#endif /* THC_MIDIEXPORT_H */
