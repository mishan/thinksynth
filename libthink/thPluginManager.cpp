#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>

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
	int fd;

	/* Use the default path first */
	sprintf(path, "%s%s%s", PLUGIN_PATH, name, PLUGPOSTFIX);
	
	/* Check for existence in the expected place */
	fd = open (path, O_RDONLY);

	if (fd == -1) { /* File existeth not */
		fprintf (stderr, "Warning: %s not found, looking in plugins/\n", path);
		sprintf (path, "plugins/%s%s", name, PLUGPOSTFIX);
	}	
		
	return path;
}

// TODO: Return values.. should we care about them? consider making void?
int thPluginManager::LoadPlugin (char *name)
{
	thPlugin *plugin;
	char *path;

	path = GetPath(name);

	plugin = new thPlugin (path);
	delete path;

	if (plugin->GetState() == thNotLoaded) {	/* something messed up */
		delete plugin;
		return 1;
	}
	
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
