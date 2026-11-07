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

/* The loudest one voice of a compiled patch gets, at velocity 127 at its
   knobs' defaults, over the two octaves from C4 the BOX's keys span: held
   a second and let ring out two more. generate.sh builds it once a voice,
   VOICE its type with its header included, and scales each patch by it. */

#include <math.h>
#include <stdio.h>

int
main ()
{
    static VOICE v;
    const VOICE::Knobs k;
    float l[256], r[256];
    float *const out[2] = { l, r };
    float most = 0;

    for (int note = 60; note <= 84; note += 2)
    {
        v.start((float)note, 127);
        for (int i = 0; i < 3 * (int)VOICE::rate / 256; i++)
        {
            if (i == (int)VOICE::rate / 256)
                v.release();
            if (!v.render(k, out, 256) && i > (int)VOICE::rate / 256)
                break;
            for (int j = 0; j < 256; j++)
                most = fmaxf(most, fmaxf(fabsf(l[j]), VOICE::channels > 1 ? fabsf(r[j]) : 0));
        }
    }
    printf("%g\n", most);
    return 0;
}
