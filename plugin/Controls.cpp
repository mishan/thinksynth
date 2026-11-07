/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include "Controls.h"

#include "thinksynth_controls.h"

/* The table controlsgen wrote, as Controls. */
static std::vector<Control> fromTable (void)
{
    std::vector<Control> out;

    for (size_t i = 0; i < sizeof(kControlTable) / sizeof(kControlTable[0]);
         i++)
    {
        const ControlRow &r = kControlTable[i];
        Control c;

        c.name = r.name;
        c.label = r.label;
        c.group = r.group;
        c.units = r.units;
        c.min = r.min;
        c.max = r.max;
        c.def = r.def;
        c.step = r.step;

        /* The value names, one per line. */
        for (const char *p = r.valueNames; *p; )
        {
            const char *e = strchr(p, '\n');

            c.valueNames.push_back(std::string(p, e ? e - p : strlen(p)));
            p = e ? e + 1 : p + strlen(p);
        }

        out.push_back(c);
    }

    return out;
}

const std::vector<Control> &controls (void)
{
    static const std::vector<Control> c = fromTable();

    return c;
}

const char *controlsDescription (void)
{
    return kControlDescription;
}
