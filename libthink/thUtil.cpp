/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <cstdlib>
#include <cstring>

#include <filesystem>

#include "thUtil.h"

static int RangeArray[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000,
                           100000000, 1000000000};

static int RangeSize = sizeof(RangeArray)/sizeof(int);

int thUtil::getNumLength (int num)
{
    /* abs(INT_MIN) is undefined -- there is no positive counterpart in int.
       Widen first, then clamp back into the range the table covers. */
    long long wide = num;

    if (wide < 0)
        wide = -wide;

    if (wide > RangeArray[RangeSize - 1])
        return RangeSize + 1;

    int i;

    for (i = 0; i < RangeSize; i++) {
        if (wide < RangeArray[i]) {
            return i+1;
        }
    }

    return RangeSize+1;
}

/* These were a strrchr(path, '/') pair -- one lifted from ircd-hybrid, one
 * from Lars Wirzenius -- which is correct on Unix and wrong everywhere else:
 * a Windows path separator is a backslash, and "C:file" is relative to the
 * current directory *of drive C*, which no amount of slash-hunting will tell
 * you. std::filesystem knows all of that per platform.
 *
 * Checked case by case against the old implementations. Behaviour on Unix is
 * unchanged, including the odd corners:
 *
 *     basename("foo")   -> "foo"      dirname("foo")   -> ""
 *     basename("a/b")   -> "b"        dirname("a/b")   -> "a"
 *     basename("/foo")  -> "foo"      dirname("/foo")  -> "/"
 *     basename("foo/")  -> ""         dirname("foo/")  -> "foo"
 *     basename("/")     -> ""         dirname("/")     -> "/"
 *
 * One deliberate difference: dirname("/a//b") was "/a/" and is now "/a".
 * Duplicate separators get collapsed. Both name the same directory, and the
 * only consumers are Gtk::FileChooser::set_current_folder and the patch
 * list's display column.
 */

string thUtil::basename (const char *path)
{
    if (path == NULL)
        return "";

    return std::filesystem::path(path).filename().string();
}

string thUtil::dirname (const char *path)
{
    if (path == NULL)
        return "";

    return std::filesystem::path(path).parent_path().string();
}
