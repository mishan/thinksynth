/* $Id: test.cpp,v 1.13 2004/03/26 09:38:37 joshk Exp $ */

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

char		*desc = "Test Plugin";
thPluginState	mystate = thPassive;

extern "C" {
  int	module_init (thPlugin *plugin);
  int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
  void module_cleanup (struct module *mod);
}

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

