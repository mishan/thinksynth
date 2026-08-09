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

    struct Entry {
        string category;    /* "osc"                     */
        string name;        /* "simple"                  */
        string spelling;    /* "osc::simple", as a .dsp writes it */

        /* Filled by describe(); empty until then. */
        string desc;
        vector<Port> ports;
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

    /* A name for a new node of this type that no existing name collides with:
       "osc", then "osc2", "osc3"... Matches how the shipped DSPs name things
       (osc, osc2, mixer2) rather than inventing a scheme. */
    static string suggestName (const string &category, const string &plugin,
                               const vector<string> &taken);

private:
    vector<Entry> entries_;
    vector<string> categories_;
    map<string, vector<Entry> > byCategory_;
};

#endif /* NODE_CATALOG_H */
