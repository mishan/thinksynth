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

#include "config.h"

#include "gthConsole.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdio>
#include <iostream>

/* Point one C stream at the console, but only if it has nowhere else to go.
 *
 * `thinksynth > log.txt' hands us a perfectly good handle for stdout, and
 * reopening that on CONOUT$ would throw the redirection away and print to the
 * terminal instead -- which is the one thing the user who typed that did not
 * ask for. An unredirected stream in a GUI-subsystem process has no handle at
 * all, and that is the case worth fixing.
 */
static void reopenIfDetached (DWORD which, const char *device,
                              const char *mode, FILE *stream)
{
    const HANDLE h = GetStdHandle(which);

    if (h != NULL && h != INVALID_HANDLE_VALUE)
        return;

    /* freopen's result is the stream on success and NULL on failure, and
       there is nothing useful to do with the failure: we are trying to make
       diagnostics visible, so a diagnostic about not managing it would have
       nowhere to go either. */
    if (freopen(device, mode, stream) == NULL)
        return;
}

void gthAttachConsole (void)
{
    /* Fails when no console launched us, which is every launch from Explorer,
       a shortcut or the Start menu. Failing is the ordinary case and is not
       worth reporting. */
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    reopenIfDetached(STD_OUTPUT_HANDLE, "CONOUT$", "w", stdout);
    reopenIfDetached(STD_ERROR_HANDLE,  "CONOUT$", "w", stderr);
    reopenIfDetached(STD_INPUT_HANDLE,  "CONIN$",  "r", stdin);

    /* stderr unbuffered and stdout line-buffered.
     *
     * The reason to attach at all is to see what the program says before it
       goes wrong, and a block-buffered stream loses exactly that: the last
       thing printed before a crash is still sitting in the buffer when the
       process dies. */
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 1024);

    /* iostreams and stdio share the same destination now, so they have to
       agree about ordering -- the tree prints with both. */
    std::ios::sync_with_stdio(true);
}

#else  /* !_WIN32 */

void gthAttachConsole (void)
{
}

#endif
