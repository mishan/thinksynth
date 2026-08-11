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

#ifndef TH_UTIL_H
#define TH_UTIL_H

#include "thExport.h"

using namespace std;

#include <string>

class THINK_API thUtil {
public:
    thUtil (void) { }
    ~thUtil (void) { }

    static int getNumLength (int num);
    static string basename (const char* path);
    static string dirname (const char* path);

    /* The directory the running executable is in, or "" if that cannot be
       determined. Linux, macOS and Windows each need a different call. */
    static string exeDir (void);

    /* Find a data file that something referred to by bare name.
     *
     * A .patch says `dsp ts1.dsp' and a .dsp says nothing about where it
     * lives, so a bare name has to be looked for in several places: the
     * environment override, the current directory, the source or build tree,
     * next to the installed binary, and finally the path compiled in at
     * configure time. Returns "" if nothing matched.
     *
     *   name     the bare filename, e.g. "ts1.dsp"
     *   subdir   where files of this kind live, e.g. "dsp"
     *   envVar   an override, e.g. "THINK_DSP_PATH", or NULL
     *   fallback the compiled-in absolute path, e.g. DSP_PATH
     */
    static string findDataFile (const string &name, const string &subdir,
                                const char *envVar, const string &fallback);
};

#endif /* TH_UTIL_H */
