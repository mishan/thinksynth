/* $Id: clip.cpp,v 1.3 2003/05/17 15:27:30 ink Exp $ */

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
	float *out ;
	thArgValue *in_arg, *in_clip, *in_lowclip;
	thArgValue *out_arg;
	unsigned int i;
	float in, clip, lowclip;

	out_arg = (thArgValue *)mod->GetArg(node, "out");
	out = out_arg->allocate(windowlen);

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_clip = (thArgValue *)mod->GetArg(node, "clip");
	in_lowclip = (thArgValue *)mod->GetArg(node, "lowclip");

	for(i=0;i<windowlen;i++) {
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
		out[i] = (((in-lowclip)/(clip-lowclip))*TH_RANGE)+TH_MIN;
	}

/*	node->SetArg("out", out, windowlen); */
	return 0;
}

