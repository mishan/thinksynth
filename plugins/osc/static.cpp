/* $Id: static.cpp,v 1.10 2003/04/29 02:03:59 joshk Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Produces Random Signal";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Static module unloading\n");
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
	printf("Static plugin loaded\n");
	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	/* Seed the RNG */
	srand(time(NULL));
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	int i;
	float *out = new float[windowlen];

	for(i=0; i < (int)windowlen; i++) {
		out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
	}

	node->SetArg("out", out, windowlen);

	return 0;
}
