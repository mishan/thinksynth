/* $Id: clip.cpp,v 1.1 2003/05/13 00:32:57 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Amplifies and clips the stream";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Distortion plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Distortion plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	thArgValue *in_arg, *in_clip;
	unsigned int i;
	float in, clip;

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_clip = (thArgValue *)mod->GetArg(node, "clip");

	for(i=0;i<windowlen;i++) {
		in = (*in_arg)[i];
		clip = (*in_clip)[i] * TH_MAX;
		if(clip < 1) {
		clip = 1;
		}
		if(in > clip) {
			in = clip;
		}
		else if (in < clip*-1) {
			in = clip*-1;
		}
		out[i] = in*(TH_MAX/clip);
		//printf("DIST: %f \t%f \t%f\n", in, clip, out[i]);
	}

	node->SetArg("out", out, windowlen);
	return 0;
}

