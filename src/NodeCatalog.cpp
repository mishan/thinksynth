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

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <algorithm>

#include "think.h"
#include "NodeCatalog.h"

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

    string root = path;

    if (root.empty())
        root = "./";
    else if (root[root.size() - 1] != '/')
        root += '/';

    DIR *top = opendir(root.c_str());

    if (top == NULL)
        return 0;

    struct dirent *de;

    while ((de = readdir(top)) != NULL)
    {
        const string cat = de->d_name;

        if (cat == "." || cat == "..")
            continue;

        const string catPath = root + cat;

        struct stat st;

        if (stat(catPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        DIR *sub = opendir(catPath.c_str());

        if (sub == NULL)
            continue;

        bool any = false;
        struct dirent *pe;

        while ((pe = readdir(sub)) != NULL)
        {
            const string file = pe->d_name;
            const string suffix = PLUGIN_SUFFIX;

            if (file.size() <= suffix.size() ||
                file.compare(file.size() - suffix.size(),
                             suffix.size(), suffix) != 0)
                continue;

            Entry e;

            e.category = cat;
            e.name = file.substr(0, file.size() - suffix.size());
            e.spelling = cat + "::" + e.name;

            entries_.push_back(e);
            byCategory_[cat].push_back(e);

            any = true;
        }

        closedir(sub);

        if (any)
            categories_.push_back(cat);
    }

    closedir(top);

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

    for (int k = 0; k < p->argCount(); k++)
    {
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
