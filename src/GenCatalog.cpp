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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "DspCatalog.h"
#include "GenCatalog.h"
#include "JsonOut.h"
#include "thcGenEdit.h"

namespace fs = std::filesystem;

GenCatalog::GenCatalog (void)
{
}

namespace {
    struct ByTitle {
        bool operator() (const GenCatalog::Entry &a,
                         const GenCatalog::Entry &b) const
        {
            if (a.name != b.name)
                return a.name < b.name;

            return a.file < b.file;
        }
    };

    string fold (const string &s)
    {
        string out = s;

        for (string::size_type i = 0; i < out.size(); i++)
            out[i] = (char)tolower((unsigned char)out[i]);

        return out;
    }
}

void GenCatalog::clear (void)
{
    entries_.clear();
    groups_.clear();
    byGroup_.clear();
}

int GenCatalog::scan (const string &dir)
{
    clear();

    const fs::path root = dir.empty() ? fs::path(".") : fs::path(dir);

    std::error_code ec;

    /* A missing gen directory is ordinary -- the list is empty and the window
       still opens -- so the non-throwing overloads. */
    if (!fs::is_directory(root, ec))
        return 0;

    vector<fs::path> files;

    for (const auto &f : fs::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (f.path().extension() == ".gen")
            files.push_back(f.path());
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        Entry e;

        e.path = files[i].string();
        e.file = files[i].filename().string();

        thcGenEdit::Doc doc;
        string why;

        /* A piece that will not index still gets a row, drawn from its
           filename. What a broken file wants is to be seen and opened, which
           is how somebody finds out what is wrong with it. */
        if (thcGenEdit::describe(e.path, doc, why) == thcGenEdit::OK)
        {
            e.name = doc.name;
            e.desc = doc.description;
            e.author = doc.author;
            e.category = doc.category;
        }

        if (e.name.empty())
            e.name = files[i].stem().string();

        entries_.push_back(e);
        byGroup_[groupOf(e)].push_back(e);
    }

    index();

    return (int)entries_.size();
}

string GenCatalog::groupOf (const Entry &e)
{
    return e.category.empty() ? DspCatalog::UNCATEGORIZED : e.category;
}

bool GenCatalog::matches (const Entry &e, const string &needle)
{
    if (needle.empty())
        return true;

    const string hay = fold(e.name + " " + e.desc + " " + e.file);

    return hay.find(fold(needle)) != string::npos;
}

void GenCatalog::index (void)
{
    groups_.clear();

    for (map<string, vector<Entry> >::iterator i = byGroup_.begin();
         i != byGroup_.end(); ++i)
    {
        sort(i->second.begin(), i->second.end(), ByTitle());

        if (i->first != DspCatalog::UNCATEGORIZED)
            groups_.push_back(i->first);
    }

    sort(groups_.begin(), groups_.end());

    /* Last, and only if anything is in it, for DspCatalog::index's reason. */
    if (byGroup_.find(DspCatalog::UNCATEGORIZED) != byGroup_.end())
        groups_.push_back(DspCatalog::UNCATEGORIZED);
}

const vector<GenCatalog::Entry> &
GenCatalog::inGroup (const string &group) const
{
    static const vector<Entry> empty;

    map<string, vector<Entry> >::const_iterator i = byGroup_.find(group);

    return (i == byGroup_.end()) ? empty : i->second;
}

static void entryToJson (string &out, const GenCatalog::Entry &e)
{
    /* The path is deliberately not in the dump. It is where the file happened
       to be found, which differs between a source tree and a MEMFS copy of
       one -- and a parity gate over two builds must compare what the files
       say, not where they were. */
    out += "{\"file\":";
    jsonString(out, e.file);
    out += ",\"name\":";
    jsonString(out, e.name);
    out += ",\"desc\":";
    jsonString(out, e.desc);
    out += ",\"author\":";
    jsonString(out, e.author);
    out += ",\"category\":";
    jsonString(out, e.category);
    out += "}";
}

string genCatalogToJson (const GenCatalog &catalog)
{
    string out = "{\"groups\":[";

    for (size_t g = 0; g < catalog.groups().size(); g++)
    {
        const string &group = catalog.groups()[g];
        const vector<GenCatalog::Entry> &list = catalog.inGroup(group);

        if (g)
            out += ",";

        out += "{\"name\":";
        jsonString(out, group);
        out += ",\"entries\":[";

        for (size_t i = 0; i < list.size(); i++)
        {
            if (i)
                out += ",";

            entryToJson(out, list[i]);
        }

        out += "]}";
    }

    out += "]}";

    return out;
}
