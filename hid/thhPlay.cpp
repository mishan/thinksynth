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

#include <filesystem>
#include <fstream>
#include <sstream>

#include "thhPlay.h"

void
thhPlay (thSynth &synth, const std::vector<thhAction> &acts,
         const std::function<void (const thhAction &)> &heard)
{
    for (size_t i = 0; i < acts.size(); i++)
    {
        const thhAction &a = acts[i];

        if (a.kind == thhAction::NOTE_ON)
            synth.addNote(a.channel, a.note, a.vel);
        else if (a.kind == thhAction::NOTE_OFF)
            synth.delNote(a.channel, a.note);
        else
        {
            /* Looked up every time: a patch loaded since would leave a kept
               pointer pointing at an arg that has gone. */
            thArg *arg = synth.getChanArg(a.channel, a.arg);

            /* setValue is the GUI thread's only for a one-element arg. */
            if (arg && arg->len() == 1)
                arg->setValue((float)thhActionValue(a, arg->min(), arg->max()));
            continue;
        }

        if (heard)
            heard(a);
    }
}

bool
thhLoadProfile (const std::string &path, const thhControls &controls,
                const char *fallback, thcHidBlock &out, std::string &err)
{
    std::error_code ec;
    std::string ignored;
    int line = 0;

    err.clear();
    if (std::filesystem::exists(path, ec))
    {
        std::ifstream in(path.c_str());
        std::ostringstream text;
        thhMapper check;
        std::vector<thhAction> none;

        text << in.rdbuf();
        if (!in)
            err = path + ": can't read it";
        else if (thcHidParseProfile(text.str(), out, err, line) &&
                 check.configure(out, controls, none, err, line))
            return true;
        else
            err = path + ":" + std::to_string(line) + ": " + err;
    }

    thcHidParseProfile(fallback, out, ignored, line);
    return err.empty();
}
