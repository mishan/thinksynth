#define USE_PLUGIN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char	*desc = "Produces Random Signal";

extern "C" int	module_init (int version, thPlugin *plugin);
extern "C" int	module_callback (void *node, void *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
  printf("Static module unloading\n");
}

int module_init (int version, thPlugin *plugin)
{
  printf("Static plugin loaded\n");
  srand(time(NULL));
  return 0;
}

int module_callback (void *node, void *mod, unsigned int windowlen)
{
  int i;
  float *out = new float[windowlen];

  for(i=0;i<(int)windowlen;i++) {
    out[i] = TH_RANGE*(rand()/(RAND_MAX+1.0))+TH_MIN;
  }

  ((thNode *)node)->SetArg("out", out, windowlen);

  delete[] out;
  return 0;
}
