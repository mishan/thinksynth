/* $Id: static.cpp,v 1.11 2003/05/11 08:57:36 ink Exp $ */

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
	float *last = new float[2];
	thArgValue *in_sample;  /* How often to change the random number */
	thArgValue *in_last;  /* So sample can be consistant over windows */

	in_sample = (thArgValue *)mod->GetArg(node, "sample");
	in_last = (thArgValue *)mod->GetArg(node, "last");

	last[0] = (*in_last)[0];
	last[1] = (*in_last)[1];

	for(i=0; i < (int)windowlen; i++) {
		if(++last[0] > (*in_sample)[i]) {
			out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
			last[0] = 0;
			last[1] = out[i];
		} else {
			out[i] = last[1];
		}
	}

	node->SetArg("out", out, windowlen);
	node->SetArg("last", last, 2);

	return 0;
}
