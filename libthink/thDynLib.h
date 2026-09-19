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

#ifndef TH_DYNLIB_H
#define TH_DYNLIB_H 1

#include <string>

#include "thExport.h"

/* Loading a plugin at run time.
 *
 * Four calls: open a file, look up a symbol by name, close it, and say what
 * went wrong.
 *
 * THINK_API on all four. They began as thPlugin's private business and libthink
 * builds hidden-by-default, so they were not exported -- which was fine until
 * thVisual, which lives in src/ and loads the visual modules, needed the same
 * four. There is no reason for a second dlopen shim, and the seam below is
 * exactly the thing worth having only one of.
 *
 * There used to be a shim like this -- nsmodule_dl, implementing dlopen over
 * the NSModule API for Mac OS X before 10.3 -- selected by an #ifdef in
 * thPlugin.cpp with an `#error Need a dl implementation!' on the else branch.
 * It was deleted as twenty-year-dead code, and Windows promptly walked into
 * that #error, MinGW having no dlfcn.h. So the seam comes back, this time for
 * a platform that is actually in use.
 */
namespace thDynLib {

typedef void *Handle;

/* NULL on failure; lastError() then says why. */
THINK_API Handle open (const std::string &path);

/* NULL if the symbol is absent, which is not always an error -- module_cleanup
   is optional. */
THINK_API void *symbol (Handle handle, const char *name);

THINK_API void close (Handle handle);

/* Only meaningful straight after a failed open() or symbol(). */
THINK_API std::string lastError (void);

} /* namespace thDynLib */

#ifdef THINK_STATIC_PLUGINS
/* A build with nothing to dlopen.
 *
 * An AudioWorkletGlobalScope has no file system and no loader, so the
 * browser build links every plugin into the one module and generates this
 * table (wasm/web). open() looks a name up in it -- the name the host
 * hands over, "osc/simple" or "composer/euclid" -- and symbol() looks in
 * that entry's symbols, which is all a caller ever does with a handle.
 * Nothing above this seam knows the difference.
 *
 * A row is a name and a list of symbols, and not the DSP ABI's four
 * fields, because there is more than one ABI behind this seam: a .dsp
 * node's plugin exports module_init and two more, a composer exports
 * composer_init and up to eight more (libthink/thcomposer.h). Neither
 * list belongs in a file whose whole job is to stand in for dlopen, so
 * the build that writes the table writes the names too -- the version
 * byte each ABI is gated on included, since a compiled-in plugin's
 * version is the header it was compiled against.
 */
struct thStaticSymbol
{
    const char *name;
    void       *addr;
};

struct thStaticPlugin
{
    const char          *name;
    const thStaticSymbol *symbols;
    size_t                count;
};

extern const thStaticPlugin thStaticPlugins[];
extern const size_t         thStaticPluginCount;

/* The composers among them, as the paths to open() them by. A host that
   would have scanned plugins/composer/ for modules reads this instead;
   there is no directory to scan. */
extern const char *const thStaticComposers[];
extern const size_t       thStaticComposerCount;

/* And the visual modules, likewise: what a host that would have scanned
   plugins/visual/ reads instead. */
extern const char *const thStaticVisuals[];
extern const size_t       thStaticVisualCount;
#endif

#endif /* TH_DYNLIB_H */
