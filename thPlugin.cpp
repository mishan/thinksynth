#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "thPlugin.h"

thPlugin::thPlugin (const char *name, int id, bool state)
{
	plugName = strdup(name);
	plugId = id;
	plugState = state;

	plugDesc = NULL;

	MakePath();

	ModuleLoad();
}

thPlugin::~thPlugin ()
{
	ModuleUnload();

	free(plugName);
	free(plugDesc);
}

const char *thPlugin::GetName (void)
{
	return plugName;
}

const char *thPlugin::GetDesc (void)
{
	return plugDesc;
}

void thPlugin::MakePath (void)
{
	plugPath = new char[strlen(plugName) + strlen(PLUGPREFIX) + 
						strlen(PLUGPOSTFIX)];
	
	sprintf(plugPath, "%s%s%s", PLUGPREFIX, plugName, PLUGPOSTFIX);
}

int thPlugin::Fire (void)
{
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
		fprintf(stderr, "thPlugin::ModuleLoad: %s\n", (char *)dlerror());
#else
		fprintf(stderr, "thPlugin::ModuleLoad: %s%s\n", 
				"Could not load plugin: ", plugPath);
#endif /* HAVE_DLERROR */
		return 1;
	}

	module_init = (int (*)(int, thPlugin *))dlsym (plugHandle, "module_init");

	if (module_init == NULL)
		return 1;

	if (module_init (MODULE_IFACE_VER, this) != 0) {
		dlclose (plugHandle);
		return 1;
	}

	return 0;
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