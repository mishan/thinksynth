/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

char		*desc = "Converts dB to an amplitude value. Arg should be <= 0.";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
}

enum { DB,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[DB] = plugin->RegArg("db");
	args[OUT_ARG] = plugin->RegArg("out");
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *db;
	thArg *out_arg;
	unsigned int i, argnum;

	db = mod->GetArg(node, args[DB]);

	out_arg = mod->GetArg(node, args[OUT_ARG]);
	argnum = (unsigned int)db->getLen();
	out = out_arg->Allocate(argnum);

	for(i=0;i<argnum;i++) {
		out[i] = exp((*db)[i]*.11512925f); // thx vorbis
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}
