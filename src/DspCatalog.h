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

#ifndef DSP_CATALOG_H
#define DSP_CATALOG_H 1

/*
 * What can be *chosen*: every .dsp on disk, with the title and the
 * description its author already wrote, and whether it is an instrument or an
 * effect.
 *
 * NodeCatalog is this for plugins, and the shape is deliberately the same --
 * one GTK-free class, a directory walk, a list per group. What differs is
 * where the facts come from. A plugin's category is its directory and its
 * description costs a dlopen, so NodeCatalog resolves per entry on demand; a
 * .dsp's description is three lines into the file, so this reads it up front
 * and there is nothing expensive left to defer.
 *
 * READING THE HEADER AND NOT THE GRAPH. Parsing a .dsp builds a thSynthTree
 * and dlopens every plugin it names. Doing that to fill a menu would cost
 * seventy-odd dlopens to draw a list, so this goes through thLexer instead:
 * the scanner .dsp and .gen already share, walked far enough to pick up the
 * info statements and to see which node the `io' statement names. No graph is
 * built, no plugin is loaded, and a file with a syntax error in its *body*
 * still yields a usable row -- which is the other half of why a menu built by
 * loading is the wrong menu.
 *
 * INSTRUMENT OR EFFECT. The engine's answer is thSynthTree::takesInput: the
 * io node declares in0. That is a fact about the file's text as much as about
 * the loaded graph, so it is answered here from the scan, at choose time,
 * rather than at load time -- which is what lets the effect chooser offer
 * effects and the instrument chooser not. Today the distinction is enforced
 * only after the file is picked, by a dialog saying it was the wrong kind.
 *
 * NAMED THE WAY A FILE NAMES IT. An entry's `file' is what a .patch's `dsp'
 * line, a .gen's `dsp' clause and thUtil::findDataFile all spell: a bare
 * `ts1.dsp' at the top of the tree, `fx/echo.dsp' one level down. Nothing
 * here holds an absolute path, because nothing that reads a catalog wants one
 * -- the shells resolve names, and a name is what they want back.
 *
 * Free of GTK, like NodeCatalog, so scripts/dspcatalog can check it against
 * the real corpus without a display -- and free of the filesystem as well
 * through take(), for the browser, where there is no directory to walk and
 * the files arrive as text over fetch().
 */

#include <map>
#include <string>
#include <vector>

using std::map;
using std::string;
using std::vector;

class DspCatalog {
public:
    DspCatalog (void);

    struct Entry {
        /* `ts1.dsp', `fx/echo.dsp' -- the name a file names it by. */
        string file;

        /* The info statements, as written. `name' falls back to the file's
           stem, because a chooser has to draw a row for a file whose author
           left the header out and the filename is the only thing left to draw
           it with. The other three are empty when absent, and a chooser shows
           nothing rather than inventing something. */
        string name;
        string desc;
        string author;
        string category;

        /* The io node declares in0: this is a channel effect and not an
           instrument. See the header comment. */
        bool isEffect;

        Entry (void) : isEffect(false) { }
    };

    /* Walks `path' for .dsp files, at the top and one level down, and reads
     * each one's header. Returns how many it found.
     *
     * One level down because that is exactly as far as a name reaches:
     * thUtil::findDataFile joins the name it is given to each candidate
     * directory, so `fx/echo.dsp' resolves and nothing deeper is ever spelled
     * anywhere. The tree has one such directory and an install(CODE) guard in
     * CMakeLists.txt that fails the install if a second appears; this walk
     * does not enforce that and does not need to.
     *
     * A file that cannot be read or cannot be lexed is skipped rather than
     * refused: a catalog is a list of what is there, and one unreadable file
     * must not cost the other seventy-six their row. */
    int scan (const string &path);

    /* One file's text, under the name it is known by. Returns false only if
     * the text could not be lexed at all.
     *
     * For a build with no directory to walk: in the browser the shipped
     * graphs are listed by dsp/index.json and fetched, so the page has the
     * bytes and no path. Anything else holding text can use it too -- an
     * editor tab asking what the file in front of it would be called in a
     * menu, say. */
    bool take (const string &file, const string &text);

    void clear (void);

    const vector<Entry> &entries (void) const { return entries_; }

    int count (void) const { return (int)entries_.size(); }

    /* The entry for `file', or NULL. */
    const Entry *find (const string &file) const;

    /* The groups, in display order: the declared categories sorted, and
     * "Uncategorized" last if anything landed there.
     *
     * A group is not a category: a file with no `category' statement still
     * has to appear somewhere, so groupOf() falls back to the directory it
     * was found in -- `fx/' is Effects -- and then to Uncategorized. That
     * fallback is what makes a category optional rather than a schema
     * (docs/DSP_FORMAT.md). */
    const vector<string> &groups (void) const { return groups_; }

    /* The entries in one group, by title. */
    const vector<Entry> &inGroup (const string &group) const;

    /* Where `e' is filed. Static because it is a rule about an entry and not
       a thing the catalog remembers. */
    static string groupOf (const Entry &e);

    /* What Uncategorized is called, in one place, since the menus, the
       harness and the fallback all have to agree on the spelling. */
    static const char *const UNCATEGORIZED;

    /* The header of one .dsp, read. Public because the node editor and the
       harnesses want the same reading of a buffer they already hold, without
       a catalog around it. Returns false if the text could not be lexed.
       Fills in what the text says and nothing else -- `file' and the fallback
       from an absent `name' to the file's stem are the caller's, because a
       buffer does not know what it is called. */
    static bool readHeader (const string &text, Entry &out);

private:
    /* take() without the re-index, for a walk that is about to do many. */
    bool add (const string &file, const string &text);

    /* Sorts the groups and each group's entries. readdir order is whatever
       the filesystem feels like, and a menu that reorders itself between runs
       is unusable. */
    void index (void);

    vector<Entry> entries_;
    vector<string> groups_;
    map<string, vector<Entry> > byGroup_;
};

#endif /* DSP_CATALOG_H */
