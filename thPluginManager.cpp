#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "thList.h"
#include "thPlugin.h"
#include "thPluginSignaler.h"
#include "thPluginManager.h"

thPluginManager::thPluginManager ()
{
	signaler = new thPluginSignaler;
	plugins = new thList;
}

thPluginManager::~thPluginManager ()
{
	delete signaler;
	UnloadPlugins();
}

char *thPluginManager::GetPath (char *name)
{
	char *path = new char[strlen(name) + strlen(PLUGPREFIX) + 
						  strlen(PLUGPOSTFIX)];
	
	sprintf(path, "%s%s%s", PLUGPREFIX, name, PLUGPOSTFIX);

	return path;
}

int thPluginManager::LoadPlugin (char *name)
{
	int (*module_init) (int version, thPlugin *plugin);
	void *handle;
	thPlugin *plugin;
	char *path;

	/* XXX */
	int id = 0;
	bool state = true;
	/* XXX */


	path = GetPath(name);
	handle = dlopen(path, RTLD_NOW);
	
	if(handle == NULL) {
#ifdef HAVE_DLERROR
		fprintf(stderr, "thPluginManager::LoadPlugin: %s\n", 
				(char *)dlerror());
#else
		fprintf(stderr, "thPluginManager::LoadPlugin: %s%s\n", 
				"Could not load plugin: ", path);
#endif /* HAVE_DLERROR */

		delete path;
		return 1;
	}

	delete path;

	plugin = new thPlugin(name, id, state, handle);

	module_init = (int (*)(int, thPlugin *))dlsym (handle, "module_init");

	if (module_init == NULL) {
		goto err;
	}

	if (module_init (MODULE_IFACE_VER, plugin) != 0) {
		goto err;
	}

	plugins->Add(plugin);

	return 0;

  err:
	delete plugin;
	return 1;
}


void thPluginManager::UnloadPlugin(char *name)
{
	thListNode *node;

	for(node = plugins->GetHead(); node; node = node->prev) {
		thPlugin *plugin = (thPlugin *)node->data;

		if(!strcmp(plugin->GetName(), name)) {
			plugins->Remove(node);
			delete plugin;
		}
	}
}

thPlugin *thPluginManager::GetPlugin (char *name)
{
	thListNode *node;

	for(node = plugins->GetHead(); node; node = node->prev) {
		thPlugin *plugin = (thPlugin *)node->data;

		if(!strcmp(plugin->GetName(), name)) {
			return plugin;
		}
	}

	return NULL;
}

void thPluginManager::UnloadPlugins (void)
{
	thListNode *node;

	for(node = plugins->GetHead(); node; node = node->prev) {
		thPlugin *plugin = (thPlugin *)node->data;

		delete plugin;
	}

	delete plugins;
}
