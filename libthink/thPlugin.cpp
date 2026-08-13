/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include "thDynLib.h"

#include "think.h"

thPlugin::thPlugin (const string &path)
{
    path_ = path;
    state_ = NOTLOADED;

    callback_ = NULL;

    args_ = (string **)calloc(ARGCHUNK, sizeof(string *));
    argdirs_ = (ArgDir *)calloc(ARGCHUNK, sizeof(ArgDir));
    argcounter_ = 0;
    argsize_ = ARGCHUNK;

    if (moduleLoad()) { /* fail = return (1) */
        fprintf(stderr, "Couldn't load plugin %s\n", path.c_str());
    }
}

thPlugin::~thPlugin ()
{
    moduleUnload();

    /* Neither the registered arg names nor the array holding them were ever
       freed. */
    for (int i = 0; i < argcounter_; i++)
    {
        delete args_[i];
    }

    free(args_);
    free(argdirs_);
    args_ = NULL;
    argdirs_ = NULL;
    argcounter_ = 0;
    argsize_ = 0;
}

void thPlugin::fire (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    if (callback_) {
        callback_(node, mod, windowlen, samples);
    }
}

/*    ModuleLoad ()
 *
 *     precondition: path != NULL
 *
 *    postcondition: state has been set to *something*.
 *    if it can't load correctly, set it NOTLOADED so that
 *    parents et al. can deal with it. 
 */

/* here is how we register args.  plugins can register their arguments and get
   an integer index for fast lookup */
int thPlugin::regArg (const string &argname, ArgDir dir)
{
    string **newargs;
    ArgDir *newdirs;

    /* was `>' -- valid slots are 0..argsize_-1, so registering the arg that
       lands exactly on argsize_ wrote one element past the array before the
       grow check fired. */
    if (argcounter_ >= argsize_)
    {
        /* make room for more args */
        newargs = (string **)calloc(argsize_ + ARGCHUNK, sizeof(string *));
        newdirs = (ArgDir *)calloc(argsize_ + ARGCHUNK, sizeof(ArgDir));
        /* copy args over to new memory */
        memcpy(newargs, args_, argcounter_ * sizeof(string *));
        memcpy(newdirs, argdirs_, argcounter_ * sizeof(ArgDir));
        free(args_);
        free(argdirs_);

        args_ = newargs;
        argdirs_ = newdirs;
        argsize_ += ARGCHUNK;
    }

    args_[argcounter_] = new string(argname);
    argdirs_[argcounter_] = dir;
    argcounter_++;

    return (argcounter_ - 1);
}

int thPlugin::moduleLoad (void)
{
    ModuleInit module_init;
    unsigned char* plug_apiversion;
        
    handle_ = thDynLib::open(path_);
    
    if (handle_ == NULL) {

        fprintf(stderr, "thPlugin::ModuleLoad: %s: %s\n", path_.c_str(),
                thDynLib::lastError().c_str());

        goto loaderr;
    }

    /* Retrieve plugin's module_init (hopefully it exists!) */
    
    module_init = (ModuleInit)thDynLib::symbol(handle_, "module_init");

    if (module_init == NULL) {
        fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_init' symbol\n");        
        goto loaderr;
    }

    /* Verify that the API version of the plugin matches our version. */
    plug_apiversion = (unsigned char*)thDynLib::symbol(handle_, "apiversion");

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
    
    callback_ = (Callback)thDynLib::symbol(handle_, "module_callback");
    
    /* Ensure that plugin's callback exists */
    
    if (callback_ == NULL) {
        fprintf(stderr, "thPlugin::ModuleLoad: Could not find 'module_callback' symbol\n");
        goto loaderr;
    }

    return 0;

loaderr:
    /* Every failure below the open leaked the handle: state_ was set and the
       function returned without closing. On Windows that also keeps the DLL
       file locked until the process exits, so a plugin cannot be replaced
       while thinksynth is running. */
    if (handle_ != NULL)
    {
        thDynLib::close(handle_);
        handle_ = NULL;
    }

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
    module_cleanup = (ModuleCleanup)thDynLib::symbol(handle_, "module_cleanup");
    
    /* ... only if it exists */
    if (module_cleanup != NULL) {
        module_cleanup (this);
    }

    /* Finally, unload the plugin */
    thDynLib::close(handle_);
    handle_ = NULL;
}
