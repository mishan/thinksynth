/* $Id: clip.cpp,v 1.7 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

char		*desc = "Amplifies and clips the stream";
thPluginState	mystate = thPassive;

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
	thArg *in_arg, *in_clip, *in_lowclip;
	thArg *out_arg;
	unsigned int i;
	float in, clip, lowclip;

	out_arg = mod->GetArg(node, "out");
	out = out_arg->Allocate(windowlen);

	in_arg = mod->GetArg(node, "in");
	in_clip = mod->GetArg(node, "clip");
	in_lowclip = mod->GetArg(node, "lowclip");

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

