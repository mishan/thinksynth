/* $Id: thPlugin.cpp,v 1.29 2003/05/01 15:54:11 joshk Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#else /* libiberty */
#include <ansidecl.h>
#ifdef PARAMS
extern "C" { extern char *basename PARAMS ((const char *)); }
#else
#error no basename implementation available! get a better unix
#endif
#endif

#include <dlfcn.h>

#include "thPlugin.h"

thPlugin::thPlugin (const char *path)
{
	plugPath = strdup(path);
	plugDesc = NULL;
	plugState = thNotLoaded;

	if(ModuleLoad() == 1) { /* fail = return (1) */
		fprintf(stderr, "thPlugin::thPlugin: Failed to load plugin\n");
	}

#ifdef USE_DEBUG
	printf ("Plugin: %s\nDescription: %s\nState: ", plugPath, plugDesc);
	
	switch (plugState) {
		case thNotLoaded:
			printf ("thNotLoaded (!)\n");
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

void thPlugin::Fire (thNode *node, thMod *mod, unsigned int windowlen)
{
	plugCallback(node, mod, windowlen);
}

void thPlugin::SetDesc (const char *desc)
{
	if(plugDesc) {
		free(plugDesc);
	}
	
	plugDesc = strdup(desc);
}

/*	
 *	ModuleLoad ()
 * 	
 * 	precondition: plugPath != NULL
 * 	
 *	postcondition: plugState has been set to *something*.
 *	if it can't load correctly, set it thNotLoaded so that
 *	parents et al. can deal with it.
 */

int thPlugin::ModuleLoad (void)
{
	int (*module_init) (thPlugin *plugin);
	unsigned char* plug_apiversion;
	
	plugHandle = dlopen(plugPath, RTLD_NOW);
	
	if(plugHandle == NULL) {

#ifdef HAVE_DLERROR
		fprintf(stderr, "thPlugin::ModuleLoad: %s\n", dlerror());
#else
		fprintf(stderr, "thPlugin::ModuleLoad: Unable to load plugin: %s", plugPath);
#endif /* HAVE_DLERROR */

		goto loaderr;
	}

	/* Retrieve plugin's module_init (hopefully it exists!) */
	
	module_init = (int (*)(thPlugin *))dlsym (plugHandle, "module_init");

	if (module_init == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_init' symbol\n");		
		goto loaderr;
	}

	/* Verify that the API version of the plugin matches our version. */
	plug_apiversion = (unsigned char*)dlsym(plugHandle, "apiversion");

	if (plug_apiversion == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: API version symbol missing\n");
		goto loaderr;
	}

	if (*plug_apiversion != MODULE_IFACE_VER) {
		fprintf(stderr, "thPlugin::ModuleLoad: version mismatch: thinksynth compiled with API v%d, %s compiled with v%d\n", MODULE_IFACE_VER, basename(plugPath), (short)(*plug_apiversion));
		goto loaderr;
	}
	
	/* We're semi sure that nothing bad is going to happen, so let's initialize the plugin. */

	if (module_init (this) != 0) {
		fprintf (stderr, "thPlugin::ModuleLoad: plugin initialization exited with an error\n");
		goto loaderr;
	}

	/* module_init MUST call SetState on the Plugin (this) that we pass to it - so fail
	 * if we're still thNotLoaded after our recent invocation of module_init */
	
	if (plugState == thNotLoaded) {
		fprintf(stderr, "thPlugin::ModuleLoad: Plugin didn't set state, aborting\n");
		goto loaderr;
	}
	
	plugCallback = (void (*)(thNode *, thMod *, unsigned int))dlsym(plugHandle, "module_callback");
	
	/* Ensure that plugin's callback exists */
	
	if(plugCallback == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_callback' symbol\n");
		goto loaderr;
	}

	return 0;

loaderr:
	plugState = thNotLoaded;
	return 1;
}

void thPlugin::ModuleUnload (void)
{
	if (plugState == thNotLoaded) { /* don't unload what is not loaded! */
		return;
	}
	
	void (*module_cleanup) (thPlugin *plug);

	/* Invoke the plugin's module_cleanup ... */
	module_cleanup = (void (*)(thPlugin *))dlsym (plugHandle, "module_cleanup");
	
	/* ... only if it exists */
	if (module_cleanup != NULL) {
		module_cleanup (this);
	}

	/* Finally, unload the plugin */
	dlclose(plugHandle);
}
