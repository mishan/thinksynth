/* $Id: print.cpp,v 1.7 2004/05/26 00:14:04 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

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

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
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

