#define USE_PLUGIN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

// XXX   #include "thnodes.h"

char	*name = "test";
char	*desc = "Test Plugin";

extern "C" int	module_init (int version, thPlugin *plugin);
extern "C" int	module_callback (void *node, void *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
  printf("Sample module unloading\n");
}

int module_init (int version, thPlugin *plugin)
{
  printf("test plugin loaded\n");
  return 0;
}

int module_callback (void *node, void *mod, unsigned int windowlen)
{
  printf("TEST!!\n");
  return 0;
}

