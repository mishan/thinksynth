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

char        *desc = "Prints 'in'";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_ARG };

int args[IN_ARG + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    thArg *in_arg;
    unsigned int i;
    const char *nodename = node->name().c_str();

    in_arg = mod->getArg(node, args[IN_ARG]);

    printf("Printing Node %s:\n", nodename); 

    for(i = 0; i < windowlen; i++)
    {
        printf("%f \t", (*in_arg)[i]);
    }

    printf("\n");

    return 0;
}
