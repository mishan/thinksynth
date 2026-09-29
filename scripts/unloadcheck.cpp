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
 * unloadcheck -- a module that links think_embedded can be unloaded.
 *
 *   scripts/unloadcheck scripts/unloadcheck-module.so
 *
 * An audio plugin built on think_embedded is a module a DAW dlopens and,
 * when the last instance goes, dlcloses. glibc keeps a module mapped for
 * as long as anything pins it -- a thread_local with a destructor on a
 * thread that is still alive, a STB_GNU_UNIQUE symbol, a symbol another
 * library bound to -- and dlclose then quietly does nothing. The loader's
 * error text was once a thread_local std::string, and one failed plugin
 * lookup was enough.
 *
 * The module (unloadcheck-module.cpp) exports one function, which makes a
 * synth, loads a graph from text, plays a window of it, and asks for a
 * plugin that does not exist. This opens it, calls that, closes it, and
 * asks with RTLD_NOLOAD whether it is still there.
 *
 * Linux only: RTLD_NOLOAD and the unload rules are glibc's.
 *
 * Exit status is 0 if the module unloaded, 1 if it did not, 2 if it could
 * not be opened or run.
 */

#include <dlfcn.h>
#include <stdio.h>

int main (int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: %s MODULE\n", argv[0]);
        return 2;
    }

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);

    if (handle == NULL)
    {
        printf("FAIL  %s\n", dlerror());
        return 2;
    }

    typedef int (*Probe)(void);
    Probe probe = (Probe)dlsym(handle, "unloadcheck_probe");

    if (probe == NULL || !probe())
    {
        printf("FAIL  the module's probe did not run\n");
        return 2;
    }

    dlclose(handle);

    if (dlopen(argv[1], RTLD_NOW | RTLD_NOLOAD) != NULL)
    {
        printf("FAIL  %s is still mapped after dlclose\n", argv[1]);
        return 1;
    }

    printf("ok    %s unloaded\n", argv[1]);

    return 0;
}
