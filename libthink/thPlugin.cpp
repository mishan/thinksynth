/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef USING_DARWIN
#  include "nsmodule_dl.h"
# else
#  error Need a dl implementation!
# endif
#endif

#include "think.h"

thPlugin::thPlugin (const string &path)
{
	path_ = path;
	state_ = NOTLOADED;

	callback_ = NULL;

	args_ = (string **)calloc(ARGCHUNK, sizeof(string *));
	argcounter_ = 0;
	argsize_ = ARGCHUNK;

	if (moduleLoad()) { /* fail = return (1) */
		fprintf(stderr, "Couldn't load plugin %s\n", path.c_str());
	}
}

thPlugin::~thPlugin ()
{
	moduleUnload();
}

void thPlugin::fire (thNode *node, thSynthTree *mod, unsigned int windowlen,
					 unsigned int samples)
{
	if (callback_) {
		callback_(node, mod, windowlen, samples);
	}
}

/*	ModuleLoad ()
 *
 * 	precondition: path != NULL
 *
 *	postcondition: state has been set to *something*.
 *	if it can't load correctly, set it NOTLOADED so that
 *	parents et al. can deal with it. 
 */

/* here is how we register args.  plugins can register their arguments and get
   an integer index for fast lookup */
int thPlugin::regArg (const string &argname)
{
	string **newargs;

	if (argcounter_ > argsize_)
	{
		/* make room for more args */
		newargs = (string **)calloc(argsize_ + ARGCHUNK, sizeof(string *));
		/* copy args over to new memory */
		memcpy(newargs, args_, argcounter_ * sizeof(string *));
		free(args_);

		args_ = newargs;
		argsize_ += ARGCHUNK;
	}

	args_[argcounter_] = new string(argname);
	argcounter_++;

	return (argcounter_ - 1);
}

int thPlugin::moduleLoad (void)
{
	ModuleInit module_init;
	unsigned char* plug_apiversion;
        
	handle_ = dlopen(path_.c_str(), RTLD_NOW);
	
	if (handle_ == NULL) {

#ifdef HAVE_DLERROR
		fprintf(stderr, "thPlugin::ModuleLoad: %s\n", dlerror());
#else
		fprintf(stderr, "thPlugin::ModuleLoad: Unable to load plugin: %s",
				path_.c_str());
#endif /* HAVE_DLERROR */

		goto loaderr;
	}

	/* Retrieve plugin's module_init (hopefully it exists!) */
	
	module_init = (ModuleInit)dlsym (handle_, "module_init");

	if (module_init == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_init' symbol\n");		
		goto loaderr;
	}

	/* Verify that the API version of the plugin matches our version. */
	plug_apiversion = (unsigned char*)dlsym(handle_, "apiversion");

	if (plug_apiversion == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: API version symbol missing\n");
		goto loaderr;
	}

	if (*plug_apiversion != MODULE_IFACE_VER) {
		fprintf(stderr, "thPlugin::ModuleLoad: version mismatch: thinksynth compiled with API v%d, %s compiled with v%d\n", MODULE_IFACE_VER,
				path_.c_str(), (short)(*plug_apiversion));
		goto loaderr;
	}
	
	/* We're semi sure that nothing bad is going to happen, so let's initialize
	   the plugin. */

	if (module_init (this) != 0)
	{
		fprintf (stderr, "thPlugin::ModuleLoad: plugin initialization exited with an error\n");
		goto loaderr;
	}

	/* module_init MUST call setState on the Plugin (this) that we pass to it 
	   - so fail
	 * if we're still NOTLOADED after our recent invocation of module_init */
	
	if (state_ == NOTLOADED) {
		fprintf(stderr, "thPlugin::ModuleLoad: Plugin didn't set state, aborting\n");
		goto loaderr;
	}
	
	callback_ = (Callback)dlsym(handle_,"module_callback");
	
	/* Ensure that plugin's callback exists */
	
	if (callback_ == NULL) {
		fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_callback' symbol\n");
		goto loaderr;
	}

	return 0;

loaderr:
	state_ = NOTLOADED;
	return 1;
}

void thPlugin::moduleUnload (void)
{
	if (state_ == NOTLOADED) { /* don't unload what is not loaded! */
		return;
	}

	ModuleCleanup module_cleanup;

	/* Invoke the plugin's module_cleanup ... */
	module_cleanup = (ModuleCleanup)dlsym (handle_, "module_cleanup");
	
	/* ... only if it exists */
	if (module_cleanup != NULL) {
		module_cleanup (this);
	}

	/* Finally, unload the plugin */
	dlclose(handle_);
}
