#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"

thPluginManager::thPluginManager ()
{
}

thPluginManager::~thPluginManager ()
{
	UnloadPlugins();
}

/* Caller must free!!! */
char *thPluginManager::GetPath (char *name)
{
	char *path = new char[strlen(name) + strlen(PLUGIN_PATH) + 
						  strlen(PLUGPOSTFIX) + 1];
	
	sprintf(path, "%s%s%s", PLUGIN_PATH, name, PLUGPOSTFIX);

	return path;
}

int thPluginManager::LoadPlugin (char *name)
{
	thPlugin *plugin;
	char *path;

	/* XXX */
	int id = 0;
	bool state = true;
	/* XXX */


	path = GetPath(name);

	plugin = new thPlugin (path, id, state);
	delete path;
	plugins.Insert(name, plugin);

	/* XXX */
	return 0;
}


void thPluginManager::UnloadPlugin(char *name)
{
	thPlugin *plugin = (thPlugin *)plugins.GetData(name);

	if(!plugin) {
		fprintf(stderr, "thPluginManager::UnloadPlugin: No such plugin '%s'\n",
				name);
		return;
	}

	plugins.Remove(name);

	delete plugin;
}

thPlugin *thPluginManager::GetPlugin (char *name)
{
	thPlugin *plugin = (thPlugin *)plugins.GetData(name);
	
	return plugin;
}

void thPluginManager::UnloadPlugins (void)
{
	/* XXX: modify thBSTree to take an option to delete data;
	   by freeing each plugin it will unload each */
/*	thListNode *node;

	for(node = plugins->GetHead(); node; node = node->prev) {
		thPlugin *plugin = (thPlugin *)node->data;

		delete plugin;
		} */
}
