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
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "think.h"
#include "thLexer.h"
#include "DspCatalog.h"
#include "JsonOut.h"

namespace fs = std::filesystem;

DspCatalog::DspCatalog (void)
{
}

namespace {
    struct ByTitle {
        bool operator() (const DspCatalog::Entry &a,
                         const DspCatalog::Entry &b) const
        {
            if (a.name != b.name)
                return a.name < b.name;

            /* Two graphs may share a title -- rpiano0.dsp and rpiano1.dsp
               both say "Resonant Piano" -- and a sort that called them equal
               would leave them in whatever order the filesystem handed them
               over. */
            return a.file < b.file;
        }
    };

    /* The info statements this reads, and the one thing it needs to know
       about a node body. Kept together because they are the whole vocabulary:
       everything else in a .dsp lexes past without being looked at. */
    bool isInfoWord (const string &w)
    {
        return w == "name" || w == "author" || w == "description" ||
               w == "category";
    }
}

void DspCatalog::clear (void)
{
    entries_.clear();
    groups_.clear();
    byGroup_.clear();
}

bool DspCatalog::readHeader (const string &text, Entry &out)
{
    vector<thLexToken> tokens;

    /* A lex that failed still hands back everything it read before the bad
       byte, and the info statements are at the top of the file, so what comes
       back is usually the whole header. The return value says the file has a
       problem; the fields say what was legible anyway, which is what a
       chooser wants to draw. */
    const bool lexed = thLexString(text, tokens);

    /* Which node declares in0, and which node the `io' statement names. Two
       passes' worth of fact gathered in one, because the two statements may
       come in either order -- every shipped graph writes `io ionode;' last,
       but nothing says it must. */
    map<string, bool> takesInput;
    string ioNode;

    size_t i = 0;

    while (i < tokens.size() && tokens[i].kind != thLexToken::END &&
           tokens[i].kind != thLexToken::ERROR)
    {
        const thLexToken &t = tokens[i];

        if (t.kind != thLexToken::WORD)
        {
            i++;
            continue;
        }

        if (isInfoWord(t.text) && i + 1 < tokens.size() &&
            tokens[i + 1].kind == thLexToken::STRING)
        {
            const string &value = tokens[i + 1].text;

            /* First one wins. The grammar takes the last of two `name'
               statements and no shipped file has two, so this is a choice
               about a file that does not exist; taking the first keeps the
               scan a single forward pass over a header rather than something
               that has to read to the end to know what it found. */
            if (t.text == "name" && out.name.empty())
                out.name = value;
            else if (t.text == "author" && out.author.empty())
                out.author = value;
            else if (t.text == "description" && out.desc.empty())
                out.desc = value;
            else if (t.text == "category" && out.category.empty())
                out.category = value;

            i += 2;
            continue;
        }

        if (t.text == "io" && i + 1 < tokens.size() &&
            tokens[i + 1].kind == thLexToken::WORD)
        {
            ioNode = tokens[i + 1].text;
            i += 2;
            continue;
        }

        if (t.text == "node" && i + 1 < tokens.size() &&
            tokens[i + 1].kind == thLexToken::WORD)
        {
            const string node = tokens[i + 1].text;

            i += 2;

            /* Past the optional `category::plugin' to the body. A node with
               no plugin is how every io node is written. */
            while (i < tokens.size() && tokens[i].kind != thLexToken::END &&
                   !(tokens[i].kind == thLexToken::PUNCT &&
                     (tokens[i].text == "{" || tokens[i].text == ";")))
                i++;

            if (i >= tokens.size() || tokens[i].kind != thLexToken::PUNCT ||
                tokens[i].text != "{")
                continue;

            i++;

            int depth = 1;
            bool in0 = false;

            while (i < tokens.size() && tokens[i].kind != thLexToken::END &&
                   tokens[i].kind != thLexToken::ERROR && depth > 0)
            {
                if (tokens[i].kind == thLexToken::PUNCT)
                {
                    if (tokens[i].text == "{")
                        depth++;
                    else if (tokens[i].text == "}")
                        depth--;
                }
                else if (depth == 1 && tokens[i].kind == thLexToken::WORD &&
                         tokens[i].text == INPUTPREFIX "0" &&
                         i + 1 < tokens.size() &&
                         tokens[i + 1].kind == thLexToken::PUNCT &&
                         tokens[i + 1].text == "=")
                {
                    /* An assignment and not a mention: `in0 = 0' is how an
                       effect declares the arg the engine writes into, and
                       `in0 = ionode->in0' on some other node is an ordinary
                       node reading it. Both are assignments; what makes the
                       first one mean something is that it is the io node's,
                       which is decided below. */
                    in0 = true;
                }

                i++;
            }

            if (in0)
                takesInput[node] = true;

            continue;
        }

        i++;
    }

    /* thSynthTree::takesInput, over text: the io node declares in0. in0 and
       not in<N> for any N, for the reason that function gives -- a graph
       declaring in1 and not in0 has a typo in it. */
    out.isEffect = !ioNode.empty() &&
                   takesInput.find(ioNode) != takesInput.end();

    return lexed;
}

bool DspCatalog::add (const string &file, const string &text)
{
    Entry e;

    e.file = file;

    const bool lexed = readHeader(text, e);

    if (e.name.empty())
    {
        /* The filename, without its directory or its extension. A row has to
           say something, and a file whose author left the header out has only
           this. */
        string stem = file;

        const string::size_type slash = stem.find_last_of('/');

        if (slash != string::npos)
            stem = stem.substr(slash + 1);

        const string::size_type dot = stem.find_last_of('.');

        if (dot != string::npos && dot > 0)
            stem = stem.substr(0, dot);

        e.name = stem;
    }

    entries_.push_back(e);
    byGroup_[groupOf(e)].push_back(e);

    return lexed;
}

bool DspCatalog::take (const string &file, const string &text)
{
    const bool lexed = add(file, text);

    index();

    return lexed;
}

int DspCatalog::scan (const string &path)
{
    clear();

    const fs::path root = path.empty() ? fs::path(".") : fs::path(path);

    std::error_code ec;

    /* A missing dsp directory is ordinary -- the chooser is empty and the
       window still opens -- so the non-throwing overloads throughout. */
    if (!fs::is_directory(root, ec))
        return 0;

    vector<string> files;

    for (const auto &top : fs::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (top.is_directory(ec))
        {
            const string dir = top.path().filename().string();

            for (const auto &f : fs::directory_iterator(top.path(), ec))
            {
                if (ec)
                    break;

                if (f.path().extension() == ".dsp")
                    files.push_back(dir + "/" + f.path().filename().string());
            }

            /* Not fatal, and not the next directory's problem. */
            ec.clear();

            continue;
        }

        if (top.path().extension() == ".dsp")
            files.push_back(top.path().filename().string());
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        std::ifstream in((root / files[i]).string(),
                         std::ios::in | std::ios::binary);

        if (!in)
            continue;

        std::ostringstream text;

        text << in.rdbuf();

        add(files[i], text.str());
    }

    index();

    return (int)entries_.size();
}

string DspCatalog::groupOf (const Entry &e)
{
    if (!e.category.empty())
        return e.category;

    /* The directory, for a file that does not say. `fx' is the one the tree
       has and the one the .gen format spells out loud, so it is named the way
       a menu would name it rather than the way the path does. Anything else
       one level down keeps its own name, capitalized -- there is nothing
       there today, and a directory somebody adds should show up as itself
       rather than as Uncategorized. */
    const string::size_type slash = e.file.find_last_of('/');

    if (slash == string::npos)
        return UNCATEGORIZED;

    string dir = e.file.substr(0, slash);

    if (dir == "fx")
        return "Effects";

    if (!dir.empty())
        dir[0] = (char)toupper((unsigned char)dir[0]);

    return dir;
}

namespace {
    string fold (const string &s)
    {
        string out = s;

        for (string::size_type i = 0; i < out.size(); i++)
            out[i] = (char)tolower((unsigned char)out[i]);

        return out;
    }
}

bool DspCatalog::matches (const Entry &e, bool effects, const string &needle)
{
    if (e.isEffect != effects)
        return false;

    if (needle.empty())
        return true;

    const string hay = fold(e.name + " " + e.desc + " " + e.file);

    return hay.find(fold(needle)) != string::npos;
}

void DspCatalog::index (void)
{
    groups_.clear();

    for (map<string, vector<Entry> >::iterator i = byGroup_.begin();
         i != byGroup_.end(); ++i)
    {
        sort(i->second.begin(), i->second.end(), ByTitle());

        if (i->first != UNCATEGORIZED)
            groups_.push_back(i->first);
    }

    sort(groups_.begin(), groups_.end());

    /* Last, and only if anything is in it. It is where a file lands when
       nothing said where it goes, and a menu that opens with it reads as
       though that is the normal case. */
    if (byGroup_.find(UNCATEGORIZED) != byGroup_.end())
        groups_.push_back(UNCATEGORIZED);
}

const vector<DspCatalog::Entry> &
DspCatalog::inGroup (const string &group) const
{
    static const vector<Entry> empty;

    map<string, vector<Entry> >::const_iterator i = byGroup_.find(group);

    return (i == byGroup_.end()) ? empty : i->second;
}

const DspCatalog::Entry *DspCatalog::find (const string &file) const
{
    for (size_t i = 0; i < entries_.size(); i++)
        if (entries_[i].file == file)
            return &entries_[i];

    return NULL;
}

static void entryToJson (string &out, const DspCatalog::Entry &e)
{
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
    out += ",\"effect\":";
    out += e.isEffect ? "true" : "false";
    out += "}";
}

string dspCatalogToJson (const DspCatalog &catalog)
{
    string out = "{\"groups\":[";

    for (size_t g = 0; g < catalog.groups().size(); g++)
    {
        const string &group = catalog.groups()[g];
        const vector<DspCatalog::Entry> &list = catalog.inGroup(group);

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
