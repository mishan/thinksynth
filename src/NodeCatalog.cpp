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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "think.h"
#include "NodeCatalog.h"

namespace fs = std::filesystem;

NodeCatalog::NodeCatalog (void)
{
}

namespace {
    struct ByName {
        bool operator() (const NodeCatalog::Entry &a,
                         const NodeCatalog::Entry &b) const
        {
            return a.name < b.name;
        }
    };
}

int NodeCatalog::scan (const string &path)
{
    entries_.clear();
    categories_.clear();
    byCategory_.clear();

    const fs::path root = path.empty() ? fs::path(".") : fs::path(path);

    std::error_code ec;

    /* A missing plugin root is ordinary -- the palette is empty and the
       editor still opens -- so the non-throwing overloads throughout. */
    if (!fs::is_directory(root, ec))
        return 0;

    for (const auto &catEntry : fs::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (!catEntry.is_directory(ec))
            continue;

        const string cat = catEntry.path().filename().string();

        bool any = false;

        for (const auto &f : fs::directory_iterator(catEntry.path(), ec))
        {
            if (ec)
                break;

            if (f.path().extension() != PLUGIN_SUFFIX)
                continue;

            Entry e;

            e.category = cat;
            e.name = f.path().stem().string();
            e.spelling = cat + "::" + e.name;

            entries_.push_back(e);
            byCategory_[cat].push_back(e);

            any = true;
        }

        if (any)
            categories_.push_back(cat);
    }

    /* readdir order is whatever the filesystem feels like, and a palette that
       reorders itself between runs is unusable. */
    sort(categories_.begin(), categories_.end());

    for (map<string, vector<Entry> >::iterator i = byCategory_.begin();
         i != byCategory_.end(); ++i)
        sort(i->second.begin(), i->second.end(), ByName());

    return (int)entries_.size();
}

const vector<NodeCatalog::Entry> &
NodeCatalog::inCategory (const string &category) const
{
    static const vector<Entry> empty;

    map<string, vector<Entry> >::const_iterator i = byCategory_.find(category);

    return (i == byCategory_.end()) ? empty : i->second;
}

bool NodeCatalog::describe (const string &spelling, thPluginManager *pm,
                            Entry &out)
{
    if (pm == NULL)
        return false;

    /* The manager wants a path-shaped name, "osc/simple", where the .dsp and
       everything the user sees says "osc::simple". */
    string wanted = spelling;

    const string::size_type sep = wanted.find("::");

    if (sep == string::npos)
        return false;

    out.category = wanted.substr(0, sep);
    out.name = wanted.substr(sep + 2);
    out.spelling = spelling;

    const string path = out.category + "/" + out.name;

    thPlugin *p = pm->getPlugin(path);

    if (p == NULL)
    {
        if (pm->loadPlugin(path) != 0)
            return false;

        p = pm->getPlugin(path);
    }

    if (p == NULL)
        return false;

    out.desc = p->desc();
    out.ports.clear();
    out.defaults.clear();

    for (int k = 0; k < p->argCount(); k++)
    {
        /* A default on an output or on internal state would be written into the
           file and then overwritten on the first window, so only inputs. */
        if (p->argHasDefault(k) && p->getArgDir(k) == thPlugin::ARG_IN)
        {
            Default d;

            d.name = p->getArgName(k);
            d.value = p->getArgDefault(k);

            out.defaults.push_back(d);
        }

        /* Internal state is not a port, here for the same reason it is not one
           on the canvas: a delay line's ring buffer is not something to wire,
           and offering it in a palette preview would suggest otherwise. */
        if (!p->argIsPort(k))
            continue;

        Port port;

        port.name = p->getArgName(k);
        port.isInput = (p->getArgDir(k) == thPlugin::ARG_IN);

        out.ports.push_back(port);
    }

    out.resolved = true;

    return true;
}

string NodeCatalog::suggestName (const string &plugin,
                                 const vector<string> &taken)
{
    /* The plugin's own name, then a number. The shipped DSPs name nodes this
       way -- osc, osc2, mixer, mixer2, map1, map2 -- so a graph the editor
       adds to goes on looking like one a person wrote. */
    string base = plugin;

    /* A name has to be a WORD to the lexer: letters, digits, underscore. */
    for (string::size_type i = 0; i < base.size(); i++)
        if (!isalnum((unsigned char)base[i]) && base[i] != '_')
            base[i] = '_';

    if (base.empty() || isdigit((unsigned char)base[0]))
        base = "n" + base;

    bool clash = false;

    for (size_t i = 0; i < taken.size(); i++)
        if (taken[i] == base)
        { clash = true; break; }

    if (!clash)
        return base;

    for (int n = 2; n < 10000; n++)
    {
        char buf[16];

        snprintf(buf, sizeof(buf), "%d", n);

        const string candidate = base + buf;

        bool used = false;

        for (size_t i = 0; i < taken.size(); i++)
            if (taken[i] == candidate)
            { used = true; break; }

        if (!used)
            return candidate;
    }

    return base;
}
