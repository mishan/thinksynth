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
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef NODE_CATALOG_H
#define NODE_CATALOG_H 1

/*
 * What can be added to a graph: every plugin on disk, grouped by category.
 *
 * thPluginManager loads plugins by name on demand and has no way to enumerate
 * them, so this walks the plugin directory instead -- 11 categories, 62
 * plugins. Names come from the filesystem, which is cheap; ports and the
 * description come from dlopening the plugin, which is not, so that happens
 * per entry only when something asks.
 *
 * Free of GTK, like NodeGraph, so scripts/dspcatalog can check it against a
 * real plugin tree without a display.
 */

#include <string>
#include <vector>
#include <map>

using std::string;
using std::vector;
using std::map;

class thPluginManager;

class NodeCatalog {
public:
    NodeCatalog (void);

    struct Port {
        string name;
        bool isInput;
    };

    /* An arg the plugin declares a default for, and that default.
     *
     * Not "a suggested starting value for every arg" -- eight args across five
     * plugins, each one its own callback already special-cases zero for:
     * `if (amp_max == 0) amp_max = TH_MAX;'. Those plugins always had a
     * default; it was written where nothing could read it, so a node the editor
     * added came out saying `amp = 0' and left the reader to know that meant
     * full scale. */
    struct Default {
        string name;
        double value;
    };

    struct Entry {
        string category;    /* "osc"                     */
        string name;        /* "simple"                  */
        string spelling;    /* "osc::simple", as a .dsp writes it */

        /* Filled by describe(); empty until then. */
        string desc;
        vector<Port> ports;
        vector<Default> defaults;
        bool resolved;

        Entry (void) : resolved(false) { }
    };

    /* Walks `path' for <category>/<plugin>.so. Returns how many it found. */
    int scan (const string &path);

    const vector<string> &categories (void) const { return categories_; }

    /* The entries in one category, in name order. */
    const vector<Entry> &inCategory (const string &category) const;

    int count (void) const { return (int)entries_.size(); }

    /* Loads the plugin and fills in its description and ports. Returns false
       if it will not load -- which is worth showing rather than hiding, since
       a plugin that does not load is a plugin the palette must not offer. */
    bool describe (const string &spelling, thPluginManager *pm, Entry &out);

    /* A name for a new node that nothing in `taken' collides with: `plugin',
       then `plugin2', `plugin3'... The shipped DSPs name nodes this way --
       osc, osc2, mixer, mixer2 -- so a graph the editor adds to goes on
       looking like one a person wrote.

       `plugin' is the bare plugin name, not the `cat::plugin' spelling. The
       category used to be a parameter here and was ignored, which read as
       though it had some say in the result; `osc::simple' comes back as
       `simple'. Anything the lexer would not take in a WORD becomes an
       underscore, and a leading digit gets an `n' put in front. */
    static string suggestName (const string &plugin,
                               const vector<string> &taken);

private:
    vector<Entry> entries_;
    vector<string> categories_;
    map<string, vector<Entry> > byCategory_;
};

#endif /* NODE_CATALOG_H */
