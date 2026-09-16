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

/* See shim/glibmm.h. */

#include <time.h>

#include <random>

#include "glibmm.h"

gint64 g_get_monotonic_time (void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (gint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Only reached by a piece with no `seed' line, whose render is not meant
   to repeat -- so the one property that matters is that it differs. */
guint32 g_random_int (void)
{
    static std::random_device rd;

    return (guint32)rd();
}

void Glib::init (void)
{
}

sigc::connection Glib::SignalTimeout::connect (const sigc::slot<bool ()> &,
                                               unsigned int)
{
    return sigc::connection();
}

Glib::SignalTimeout Glib::signal_timeout (void)
{
    return SignalTimeout();
}
