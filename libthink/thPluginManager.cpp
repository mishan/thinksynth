/* $Id: thPluginManager.cpp,v 1.22 2003/04/27 09:37:48 aaronl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>

#ifdef USE_DEBUG
#include <errno.h>
#endif

#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"

thPluginManager::thPluginManager ()
{
	plugins = new thBSTree(StringCompare);
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

	if (fd < 0) { /* File existeth not */
		fprintf (stderr, "thPluginManager::GetPath: %s not found, looking in plugins/\n", path);
		
		delete path;
		path = new char[strlen("plugins/") + strlen(name) + 
						strlen(PLUGPOSTFIX) + 1];
		sprintf (path, "plugins/%s%s", name, PLUGPOSTFIX);
		if((fd = open(path, O_RDONLY)) < 0) {
			fprintf(stderr, "thPluginManager::GetPath: %s not found\n", path);
			delete path;
			return NULL;
		}
	}

	close(fd);
		
	return path;
}

/* TODO: Return values.. should we care about them? consider making void? */
int thPluginManager::LoadPlugin (char *name)
{
	thPlugin *plugin;
	char *path;

	path = GetPath(name);

	plugin = new thPlugin (path);
	delete[] path;

	if (plugin->GetState() == thNotLoaded) {	/* something messed up */
		delete plugin;
		return 1;
	}
	
	plugins->Insert((void *)name, (void *)plugin);

	return 0;
}


void thPluginManager::UnloadPlugin(char *name)
{
	thPlugin *plugin = (thPlugin *)plugins->GetData((void *)name);

	if(!plugin) {
		fprintf(stderr, "thPluginManager::UnloadPlugin: No such plugin '%s'\n", name);
		return;
	}

	plugins->Remove((void *)name);

	delete plugin;
}

thPlugin *thPluginManager::GetPlugin (char *name)
{
	thPlugin *plugin = (thPlugin *)plugins->GetData((void *)name);
	
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
