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

#ifndef GTHGTKRUNTIME_H
#define GTHGTKRUNTIME_H

#include <string>

/* Points GTK at the schemas, icon themes and pixbuf loaders shipped beside
 * the executable, when there are any. See cmake/GtkRuntime.cmake for what
 * gets shipped and gthGtkRuntime.cpp for how it is found.
 *
 * Does nothing at all when the package has no bundled GTK data, which is the
 * case for every build on Linux and for any build run from the build tree.
 * That is the point: the same binary has to work against a system GTK.
 */
namespace gthGtkRuntime
{
    /* Must be called before Gtk::Main. GLib caches the system data
     * directories on first use, so anything set after that is ignored.
     */
    void configure (void);

    /* The bundle root that configure() found, or "" if there was none. */
    std::string bundleRoot (void);

    /* `thinksynth -G'. Reports what GTK can actually reach and returns 0 if
     * all of it is reachable.
     *
     * This exists because none of the above can otherwise be tested on any
     * machine the project has. The macOS and Windows packages are the reason
     * the code exists and are exactly where it cannot be run, so the check
     * keys off the three things that break -- a schema lookup, an icon
     * lookup, a pixbuf format -- rather than off the platform, and CI runs it
     * on Linux with the system's own GTK data hidden.
     *
     * Must be called after Gtk::Main: the icon theme needs GTK initialised.
     */
    int selfTest (void);
}

#endif
