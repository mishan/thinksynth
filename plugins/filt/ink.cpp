/* $Id: ink.cpp,v 1.1 2003/05/15 09:51:26 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "`INK Filter`  Gravity-based low pass";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("inkfilt plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("inkfilt plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out = new float[windowlen];
	float *out_accel = new float[windowlen];
	float *out_last = new float[1];
	float *out_last_accel = new float[1];
	thArgValue *in_arg, *in_factor, *in_grav, *in_frict, *in_limit, *in_accel, *in_last;
	unsigned int i;
	float diff, accel;

	in_arg = (thArgValue *)mod->GetArg(node, "in");
	in_factor = (thArgValue *)mod->GetArg(node, "factor");
	in_grav = (thArgValue *)mod->GetArg(node, "grav");
	in_frict = (thArgValue *)mod->GetArg(node, "frict");
	in_limit = (thArgValue *)mod->GetArg(node, "limit");
	in_accel = (thArgValue *)mod->GetArg(node, "accel");
	in_last = (thArgValue *)mod->GetArg(node, "last");

	accel = (*in_accel)[0];
	*out_last = (*in_last)[0];
	//printf("-- %f\n", accel);
	for(i=0;i<windowlen;i++) {
		diff = (*in_arg)[i] - *out_last;
		if(fabs(*out_last) > (*in_limit)[i]) { accel -= accel/6; }
		accel += diff/((*in_factor)[i]+1);
	
		if(accel > 0) { accel -= (*in_frict)[i]; }
		if(accel < 0) { accel += (*in_frict)[i]; }
	
		if(*out_last < 0) { accel += (*in_grav)[i]; }
		if(*out_last > 0) { accel -= (*in_grav)[i]; }
	
		*out_last += accel;

		out_accel[i] = accel;

		if(*out_last > TH_MAX) { accel -= fabs(accel)/2; }
		if(*out_last < TH_MIN) { accel += fabs(accel)/2; }
		//printf("%f  %f  %f\n", diff, *out_last, accel);
		out[i] = *out_last;
	}

	out_last_accel[0] = accel;

	node->SetArg("out", out, windowlen);
	node->SetArg("aout", out_accel, windowlen);
	node->SetArg("last", out_last, 1);
	node->SetArg("accel", out_last_accel, 1);

	return 0;
}

