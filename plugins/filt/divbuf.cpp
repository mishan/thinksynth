/* $Id: divbuf.cpp,v 1.1 2003/05/13 18:59:42 ink Exp $ */

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

#define SQR(a) (a*a)

char		*desc = "Cheap IIR-ish Filter";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Divbuf plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Divbuf plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	float *out_buffer = new float[1];
	thArgValue *in_arg, *in_factor, *in_buffer;
	unsigned int i;
	float factor, buffer;

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_factor = (thArgValue *)mod->GetArg(node, "factor");
	in_buffer = (thArgValue *)mod->GetArg(node, "buffer");

	buffer = (*in_buffer)[0];

	for(i=0;i<windowlen;i++) {
	  factor = SQR(SQR((*in_factor)[i]));

	  buffer = ((*in_arg)[i] * factor) + (buffer * (1-factor));

	  out[i] = buffer;
	}

	out_buffer[0] = buffer;

	node->SetArg("out", out, windowlen);
	node->SetArg("buffer", out_buffer, 1);

	return 0;
}

