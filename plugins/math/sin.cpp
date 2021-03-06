/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char        *desc = "Sine Calculation";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    thArg *in_index, *in_wavelength, *in_amp;
    thArg *out_arg;
    unsigned int i;
    float amp, wavelength;

    in_index = mod->getArg(node, "index");
    in_wavelength = mod->getArg(node, "wavelength");
    in_amp = mod->getArg(node, "amp");

    out_arg = mod->getArg(node, "out");
    out = out_arg->allocate(windowlen);

    for(i = 0; i < windowlen; i++)
    {
        amp = (*in_amp)[i];
        wavelength = (*in_wavelength)[i];

        if (amp == 0)
        {
            amp = 1;
        }
        if (wavelength == 0)
        {
            wavelength = 1;
        }

        out[i] = sin(((*in_index)[i] / wavelength) * M_PI * 2) * amp;
    }

    return 0;
}

