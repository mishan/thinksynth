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

/* Loading a plugin at run time.
 *
 * Four calls, because that is all thPlugin needs: open a file, look up a
 * symbol by name, close it, and say what went wrong.
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
Handle open (const std::string &path);

/* NULL if the symbol is absent, which is not always an error -- module_cleanup
   is optional. */
void *symbol (Handle handle, const char *name);

void close (Handle handle);

/* Only meaningful straight after a failed open() or symbol(). */
std::string lastError (void);

} /* namespace thDynLib */

#endif /* TH_DYNLIB_H */
