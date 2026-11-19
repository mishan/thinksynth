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

#ifndef GTH_PREFS_H
#define GTH_PREFS_H

/* Lives inside Glib::get_user_config_dir()/thinksynth/, so no leading dot. */
#define PREFS_FILE "thinkrc"

/* The pre-XDG location, directly in $HOME. Read as a fallback, never written. */
#define LEGACY_PREFS_FILE ".thinkrc"

#include <sigc++/sigc++.h>

#include "thMidiRoutes.h"

class gthPrefs
{
public:
    gthPrefs (void);
    gthPrefs (const string &path);
    ~gthPrefs (void);

    static string prefsPath (void);
    static string legacyPrefsPath (void);

    static gthPrefs *instance (void) {
        if (instance_ == NULL)
            instance_ = new gthPrefs;

        return instance_;
    }

    void Set (const string &key, string **vals);
    string **Get (const string &key);

    /* One key and its values gone, so Save no longer writes it. */
    void Remove (const string &key);

    /* The keys that start with `prefix': a family of per-name keys, such
       as a MIDI route per port pattern. */
    vector<string> Keys (const string &prefix) const;

    /* A line splits its key at the first space and its values at commas,
       so text holding either -- a port name, a pattern -- is stored with
       them, and `%' and newlines, as %XX. */
    static string escape (const string &s);
    static string unescape (const string &s);

    void Load (void);
    void Save (void);

    /* `midiroute M ENGINE [LO HI]' lines: this machine's live MIDI input
       routes, the route table's machine layer. M and ENGINE count from 1,
       as a person does; ENGINE `off' silences M; LO and HI are note names
       or numbers. Setting them says so on signal_midi_routes; Save writes
       them. */
    const std::vector<thMidiRoute> &midiRoutes (void) const
    { return midiRoutes_; }
    void setMidiRoutes (const std::vector<thMidiRoute> &routes);
    sigc::signal<void (std::vector<thMidiRoute>)> &signal_midi_routes (void)
    { return midiRoutesChanged_; }

    /* One route as its line says it, and back; false, with why, for a
       line that says nothing a route can be. */
    static bool parseMidiRoute (const string &text, thMidiRoute &out,
                                string &why);
    static string formatMidiRoute (const thMidiRoute &route);

    /* What the last Load could not read, a line each with its number. */
    const vector<string> &badLines (void) const { return badLines_; }

    /* The built-in first-run configuration: a few channels with patches on
       them, named relatively and resolved through the patch search path.
       Called by Load() when there is no configuration file anywhere. Public
       so a harness can ask for it without arranging a config directory.

       False if nothing was applied -- no synth yet, or no patch would load --
       in which case the caller should not write a file either. */
    bool LoadDefaults (void);
private:
    map <string, string**> prefs_;
    string prefsPath_;
    std::vector<thMidiRoute> midiRoutes_;
    sigc::signal<void (std::vector<thMidiRoute>)> midiRoutesChanged_;
    vector<string> badLines_;

    static gthPrefs *instance_;
};

#endif /* GTH_PREFS_H */
