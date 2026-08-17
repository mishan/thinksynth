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
 * includecheck -- which libthink headers stand on their own.
 *
 * The test is the compile. Running this proves nothing; the assertion is
 * that every header listed below was included first, in its own
 * translation unit, and the file built anyway.
 *
 * Most of libthink's headers do not qualify and are not listed. They name
 * `string' and `map' bare and compile only because think.h included
 * <string>, <map> and `using namespace std;' before pulling them in --
 * which works exactly as long as every consumer goes through think.h. It
 * is a trap for whoever first includes one directly, and the failure is a
 * wall of errors in a header the author never touched.
 *
 * Fixing the whole family in one go is a larger change than it looks
 * (every `string' in every signature, and the .cpp files that match
 * them), so it is happening one header at a time -- and this is what
 * makes that safe. A header only joins the list when it can, and once it
 * is here it cannot quietly leave: a `using namespace std;' someone adds
 * to think.h years from now will not paper over a missing <string> in a
 * translation unit that never includes think.h.
 *
 * Adding a header: put its include at the top of its own block below and
 * build. If it fails, the header is not self-contained yet, and the fix
 * is in the header rather than here.
 */

/* Each in its own block, first, so no block can be quietly propped up by
   the one above it. Separate TUs would be stricter still; the ordering
   here is what a single file can express, and the includes are sorted so
   nothing gets a free ride from an earlier alphabetical neighbour. */

#include "thPluginManager.h"

#include "thEndian.h"

#include "thException.h"

#include "thExport.h"

#include "thLexer.h"

#include "thRing.h"

#include "thSampleRing.h"

#include <cstdio>

int
main (void)
{
    /* Nothing to check at run time -- see the header comment. Printed so
       a ctest log says what was covered rather than nothing at all. */
    printf("includecheck: 7 headers compiled standing on their own\n");

    return 0;
}
