#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "thPlugin.h"

thPlugin::thPlugin (const char *path, int id, bool state)
{
	plugPath = strdup(path);
	plugId = id;
	plugState = state;

	plugDesc = NULL;

	if(ModuleLoad()) {
		fprintf(stderr, "thPlugin::thPlugin: Failed to load plugin\n");
	}
}

thPlugin::~thPlugin ()
{
	ModuleUnload();

	free(plugPath);
	free(plugDesc);
}

const char *thPlugin::GetPath (void)
{
	return plugPath;
}

const char *thPlugin::GetDesc (void)
{
	return plugDesc;
}

int thPlugin::Fire (void *node, void *mod, unsigned int windowlen)
{
	plugCallback(node, mod, windowlen);

	return 0;
}

void thPlugin::SetDesc (const char *desc)
{
	if(plugDesc) {
		free(plugDesc);
	}
	
	plugDesc = strdup(desc);
}

int thPlugin::ModuleLoad (void)
{
	int (*module_init) (int version, thPlugin *plugin);

	plugHandle = dlopen(plugPath, RTLD_NOW);
	
	if(plugHandle == NULL) {
#ifdef HAVE_DLERROR
		fprintf(stderr, "thPlugin::ModuleLoad: %s\n", 
				(char *)dlerror());
#else
		fprintf(stderr, "thPlugin::ModuleLoad: %s%s\n", 
				"Could not load plugin: ", plugPath);
#endif /* HAVE_DLERROR */

		return 1;
	}

	module_init = (int (*)(int, thPlugin *))dlsym (plugHandle, "module_init");

	if (module_init == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_init' symbol\n");
		goto err;
	}

	if (module_init (MODULE_IFACE_VER, this) != 0) {
		goto err;
	}

	plugCallback = (void (*)(void *, void *, unsigned int))dlsym(plugHandle, "module_callback");
	
	if(plugCallback == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_callback' symbol\n");
		goto err;
	}

	return 0;

  err:
	return 1;
}

void thPlugin::ModuleUnload (void)
{
	void (*module_cleanup) (thPlugin *plug);

	module_cleanup = (void (*)(thPlugin *))dlsym (plugHandle, 
												  "module_cleanup");
	
	if (module_cleanup != NULL) {
		module_cleanup (this);
	}

	dlclose(plugHandle);
}
