/* $Id: mul.cpp,v 1.1 2003/05/02 21:24:20 ink Exp $ */

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

char		*desc = "Multiplies two streams";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

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
  float *out = new float[windowlen];
  thArgValue *in_0, *in_1;
  unsigned int i;

  in_0 = (thArgValue *)mod->GetArg(node->GetName(), "in0");
  in_1 = (thArgValue *)mod->GetArg(node->GetName(), "in1");

  for(i=0;i<windowlen;i++) {
	out[i] = (*in_0)[i]*((*in_1)[i]/TH_MAX);
  }

  node->SetArg("out", out, windowlen);
  return 0;
}

