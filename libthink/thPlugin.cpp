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

thPlugin::thPlugin (const char *path)
{
	plugPath = strdup(path);
	plugDesc = NULL;

	if(ModuleLoad() == 1) { /* fail = return (1) */
		fprintf(stderr, "thPlugin::thPlugin: Failed to load plugin\n");
	}

#ifdef USE_DEBUG
	printf ("Plugin: %s\nDescription: %s\nState: ", plugPath, plugDesc);
	
	switch (plugState) {
		case thNotLoaded:
			printf ("thActive\n");
			break;
		case thActive:
			printf ("thActive\n");
			break;
		case thPassive:
			printf ("thPassive\n");
			break;
	}
#endif			  
}

thPlugin::~thPlugin ()
{
	ModuleUnload();

	free(plugPath);
	free(plugDesc);
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

/*	ModuleLoad ()
 * 	precondition: plugPath != NULL
 *	postcondition: plugState has been set to *something*
 */

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

		plugState = thNotLoaded;
		return 1;
	}

	module_init = (int (*)(int, thPlugin *))dlsym (plugHandle, "module_init");

	if (module_init == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_init' symbol\n");
		plugState = thNotLoaded;
		return 1;
	}

	if (module_init (MODULE_IFACE_VER, this) != 0) {
		fprintf (stderr, "thPlugin::ModuleLoad: Interface version missing\n");
		plugState = thNotLoaded;
		return 1;
	}

	plugCallback = (void (*)(void *, void *, unsigned int))dlsym(plugHandle, "module_callback");
	
	if(plugCallback == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_callback' symbol\n");
		plugState = thNotLoaded;
		return 1;
	}

	return 0;
}

void thPlugin::ModuleUnload (void)
{
	if (plugState == thNotLoaded) { /* don't unload what is not loaded! */
		return;
	}
	
	void (*module_cleanup) (thPlugin *plug);

	module_cleanup = (void (*)(thPlugin *))dlsym (plugHandle, 
												  "module_cleanup");
	
	if (module_cleanup != NULL) {
		module_cleanup (this);
	}

	dlclose(plugHandle);
}
