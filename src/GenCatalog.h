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

#ifndef GEN_CATALOG_H
#define GEN_CATALOG_H 1

/*
 * What can be opened: every .gen in a directory, with the title, the
 * description and the category its author wrote.
 *
 * DspCatalog is this for graphs and the shape is the same on purpose -- a
 * directory walk, a list per group, no toolkit -- but it does not read the
 * headers itself. thcGenEdit::describe is the .gen reader: it indexes a file
 * by byte span so the Composer can splice edits into it, and what it indexes
 * includes the four info statements. Running the shared lexer over the same
 * bytes a second time here would be a second reading of a format that already
 * has enough of them, and the .patch story is what that costs.
 *
 * THE GROUP IS THE CATEGORY, or Uncategorized. There is no directory to fall
 * back to the way there is for `fx/': gen/ is flat and every shipped piece
 * declares one (scripts/gencheck fails one that does not, or one filed
 * outside gen/README.md's eight sections). A piece of somebody's own may say
 * anything, or nothing, and lands accordingly.
 *
 * NAMED BY PATH, unlike DspCatalog. A piece is opened by copying a file, not
 * by resolving a name against a search path -- ComposerWindow::onOpen takes a
 * path and there is no thUtil::findDataFile in the way -- so an entry carries
 * the path it was found at.
 */

#include <map>
#include <string>
#include <vector>

using std::map;
using std::string;
using std::vector;

class GenCatalog {
public:
    GenCatalog (void);

    struct Entry {
        /* The path it was found at, and its filename. */
        string path;
        string file;

        /* The info statements, as written. `name' falls back to the file's
           stem, because a menu has to draw a row for a piece whose author
           left the header out. */
        string name;
        string desc;
        string author;
        string category;
    };

    /* Walks `dir' for *.gen and reads each one's header. Returns how many it
     * found. Not recursive: gen/ is flat, and a piece somewhere else is
     * opened by path.
     *
     * A file that cannot be read or cannot be indexed is skipped rather than
     * refused -- a catalog is a list of what is there, and one broken piece
     * must not cost the others their row. */
    int scan (const string &dir);

    void clear (void);

    const vector<Entry> &entries (void) const { return entries_; }

    int count (void) const { return (int)entries_.size(); }

    /* The groups, in display order: the declared categories sorted, and
       "Uncategorized" last if anything landed there. */
    const vector<string> &groups (void) const { return groups_; }

    /* The entries in one group, by title. */
    const vector<Entry> &inGroup (const string &group) const;

    /* Where `e' is filed: its category, or Uncategorized. */
    static string groupOf (const Entry &e);

    /* Whether a menu filtered by `needle' has a row for `e'. The needle is
       matched over the title, the description and the filename, for the
       reason DspCatalog::matches gives. Here rather than in the widget so
       that what a menu offers is checkable without a display. */
    static bool matches (const Entry &e, const string &needle);

private:
    void index (void);

    vector<Entry> entries_;
    vector<string> groups_;
    map<string, vector<Entry> > byGroup_;
};

/* The catalog as JSON: the groups in display order, each with its entries.
 *
 * One writer, produced twice, for the reason dspCatalogToJson is one. */
string genCatalogToJson (const GenCatalog &catalog);

#endif /* GEN_CATALOG_H */
