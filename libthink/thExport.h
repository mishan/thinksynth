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

#ifndef TH_EXPORT_H
#define TH_EXPORT_H 1

/* Who can see what across the libthink / plugin boundary.
 *
 * Historically nobody said. The 66 plugins linked against -lm and nothing
 * else, and resolved thPlugin::regArg, thSynthTree::getArg, thArg::allocate
 * and the rest against the host process at dlopen time. That works on Linux
 * because the executable links libthink with default visibility, and it
 * worked on macOS only because configure.ac passed `-flat_namespace
 * -undefined suppress' -- two flags current ld64 has been trying to retire
 * for years. On Windows it cannot work at all: a DLL has to resolve every
 * symbol at link time.
 *
 * So plugins now link against libthink like any other consumer, and libthink
 * says what it exports.
 *
 * THINK_API goes on every class a plugin, the application or a harness can
 * touch. Annotating a class exports all of its members, which is what is
 * wanted here -- the granularity of this API is the class.
 *
 * Unix builds pair this with -fvisibility=hidden. That is the point of doing
 * it on all three platforms rather than only where it is forced: without it,
 * a missing THINK_API keeps working on Linux and only surfaces as an
 * unresolved symbol on Windows, months later and far from the cause.
 */

#if defined(_WIN32) || defined(__CYGWIN__)

# ifdef THINK_BUILDING_LIB
#  define THINK_API __declspec(dllexport)
# else
#  define THINK_API __declspec(dllimport)
# endif

/* A plugin's entry points, which the host finds with dlsym/GetProcAddress
   rather than by linking. Always exported, never imported. */
# define THINK_PLUGIN_API __declspec(dllexport)

#else

# define THINK_API        __attribute__((visibility("default")))
# define THINK_PLUGIN_API __attribute__((visibility("default")))

#endif

#endif /* TH_EXPORT_H */
