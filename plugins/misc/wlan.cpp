/* $Id: wlan.cpp,v 1.2 2004/05/11 20:36:54 ink Exp $ */

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
thPluginState	mystate = thActive;

void module_cleanup (struct module *mod)
{
	printf("WLan plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
#ifdef HAVE_IWLIB_H
	printf("WLan plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	args[OUT_ARG] = plugin->RegArg("out");

	return 0;
#else
	fprintf(stderr, "ERROR: WLan plugin compiled without libiw support!\n");

	return 1;
#endif /* HAVE_IWLIB_H */
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
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

 	out_arg = mod->GetArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(1);
	out[0] = is.qual.qual;

	return 0;
#else
	return 1;
#endif /* HAVE_IWLIB_H */
}

