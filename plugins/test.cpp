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

int	init_plug (int version, thPlugin *plugin);
int	plugin_func (void *node, void *mod, unsigned int windowlen);

int	module_init (int ver, class thPlugin plugin)
{
  /* We should do version checks and stuff here */
  return 0;
}

void module_cleanup (struct module *mod)
{
  printf("Sample module unloading\n");
}

int init_plug (int version, thPlugin *plugin)
{
  printf("test plugin loaded\n");
  return 0;
}

int plugin_func (void *node, void *mod, unsigned int windowlen)
{
  printf("TEST!!\n");
  return 0;
}

