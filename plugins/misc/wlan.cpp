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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#ifdef HAVE_IWLIB_H
#include "iwlib.h"
#endif /* HAVE_IWLIB_H */

enum {OUT_ARG};
int args[OUT_ARG + 1];

char		*desc = "Passes on a wireless interface's signal level";
thPlugin::State	mystate = thPlugin::ACTIVE;

void module_cleanup (struct module *mod)
{
}

int module_init (thPlugin *plugin)
{
#ifdef HAVE_IWLIB_H
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[OUT_ARG] = plugin->regArg("out");

	return 0;
#else
	fprintf(stderr, "ERROR: WLan plugin compiled without libiw support!\n");

	return 1;
#endif /* HAVE_IWLIB_H */
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
#ifdef HAVE_IWLIB_H
	float *out;
	thArg *out_arg;
	unsigned int i, argnum;

	static int wfd = -1;
	iwstats is;
	char *dev = "eth1";

	if (wfd < 0)
		wfd = iw_sockets_open();

	if (iw_get_stats (wfd, dev, &is, NULL, 0) == -1)
	{
		fprintf(stderr, "Could not get information for device '%s'\n", dev);
		return 1;
	}

 	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->allocate(1);
	out[0] = is.qual.qual;

	return 0;
#else
	return 1;
#endif /* HAVE_IWLIB_H */
}

