#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "thPlugin.h"

thPlugin::thPlugin (const char *name, int id, bool state, void *handle)
{
	plugName = strdup(name);
	plugId = id;
	plugState = state;
	plugDesc = NULL;
	plugHandle = handle;
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
