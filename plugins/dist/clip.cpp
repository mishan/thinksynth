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

#include "think.h"

char        *desc = "Amplifies and clips the stream";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,IN_ARG,IN_CLIP,IN_LOWCLIP };

int args[IN_LOWCLIP + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out");
    args[IN_ARG] = plugin->regArg("in");
    args[IN_CLIP] = plugin->regArg("clip");
    args[IN_LOWCLIP] = plugin->regArg("lowclip");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out ;
    thArg *in_arg, *in_clip, *in_lowclip;
    thArg *out_arg;
    unsigned int i;
    float in, clip, lowclip;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out = out_arg->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_clip = mod->getArg(node, args[IN_CLIP]);
    in_lowclip = mod->getArg(node, args[IN_LOWCLIP]);

    for(i = 0; i < windowlen; i++)
{
        in = (*in_arg)[i];
        clip = (*in_clip)[i] * TH_MAX;
        lowclip = (*in_lowclip)[i] * TH_MAX;

        if(clip < 1) {  /* Clip needs to be at least one */
            clip = 1;
        }
        if(lowclip < 1) {  /* Same for the bottom side */
            lowclip = clip;
        }
        lowclip *= -1;

        if(in > clip) {  /* Apply the clipping */
            in = clip;
        }
        else if (in < lowclip) {
            in = lowclip;
        }

        /* Map the clipped data to the data range */
        out[i] = (((in-lowclip)/(clip-lowclip))*TH_RANGE) + TH_MIN;
    }

/*    node->SetArg("out", out, windowlen); */
    return 0;
}
