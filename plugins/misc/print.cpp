/* $Id: print.cpp,v 1.5 2004/03/26 09:50:33 joshk Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "Prints 'in'";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Printer plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Printer plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	thArg *in_arg;
	unsigned int i;
	const char *nodename = node->GetName().c_str();

	in_arg = mod->GetArg(node, "in");

	printf("Printing Node %s:\n", nodename); 
	for(i=0;i<windowlen;i++) {
		printf("%f \t", (*in_arg)[i]);
	}
	printf("\n");

	return 0;
}

