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

#define SQR(a) (a*a)

char        *desc = "Cheap IIR-ish Filter";
thPlugin::State    mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

enum { OUT_ARG,INOUT_BUFFER,IN_ARG,IN_FACTOR };

int args[IN_FACTOR + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[OUT_ARG] = plugin->regArg("out");
    args[INOUT_BUFFER] = plugin->regArg("buffer");
    args[IN_ARG] = plugin->regArg("in");
    args[IN_FACTOR] = plugin->regArg("factor");
    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    float *out;
    float *out_buf;
    thArg *in_arg, *in_factor;
    thArg *out_arg;
    thArg *inout_buffer;
    
    unsigned int i;
    float factor, buffer;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    inout_buffer = mod->getArg(node, args[INOUT_BUFFER]);
    buffer = (*inout_buffer)[0];
    out = out_arg->allocate(windowlen);
    out_buf = inout_buffer->allocate(1);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_factor = mod->getArg(node, args[IN_FACTOR]);

    for(i=0;i<windowlen;i++) {
      factor = SQR(SQR((*in_factor)[i]));

      buffer = ((*in_arg)[i] * factor) + (buffer * (1-factor));

      out[i] = buffer;
    }

    out_buf[0] = buffer;

/*    node->SetArg("out", out, windowlen);
    node->SetArg("buffer", out_buffer, 1); */

    return 0;
}
