/* $Id: test.cpp,v 1.15 2004/04/08 00:34:56 misha Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

char		*desc = "Test Plugin";
thPluginState	mystate = thPassive;

void module_cleanup (struct module *mod)
{
	printf("Sample module unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("test plugin loaded\n");
	
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	printf("TEST!!\n");
	return 0;
}

