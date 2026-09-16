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

/*
 * The part of glibmm the composer host uses, for a build that has no glib.
 *
 * thcScheduler hangs a twenty-millisecond timer on the Glib main loop and
 * reads the monotonic clock to pace it, and draws a master seed from
 * g_random_int when the piece does not pin one. That is all. genwav drives
 * the scheduler through stepTransport and never runs a loop, so the timer
 * here is a connection to nothing: it can be disconnected and it never
 * fires. See glib.cpp.
 */

#ifndef TH_WASM_GLIBMM_H
#define TH_WASM_GLIBMM_H 1

#include <stdint.h>

#include <sigc++/sigc++.h>

typedef int64_t  gint64;
typedef uint32_t guint32;

gint64  g_get_monotonic_time (void);
guint32 g_random_int (void);

namespace Glib
{
    void init (void);

    struct SignalTimeout
    {
        sigc::connection connect (const sigc::slot<bool ()> &slot,
                                  unsigned int interval);
    };

    SignalTimeout signal_timeout (void);
}

#endif /* TH_WASM_GLIBMM_H */
