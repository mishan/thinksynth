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

const vector<string> thPlugin::noValues_;

thPlugin::thPlugin (const string &path)
{
    path_ = path;
    state_ = NOTLOADED;

    callback_ = NULL;

    args_.reserve(ARGCHUNK);

    if (moduleLoad()) { /* fail = return (1) */
        fprintf(stderr, "Couldn't load plugin %s\n", path.c_str());
    }
}

thPlugin::~thPlugin ()
{
    moduleUnload();

    /* The registered arg names used to be `new string' behind a calloc'd array
       and neither was ever freed. Both belong to the vector now. */
    args_.clear();
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
    args_.push_back(ArgInfo(argname, dir));

    return (int)args_.size() - 1;
}

/* Both of these take the index regArg() handed back, so a plugin says

       args[IN_WAVEFORM] = plugin->regArg("waveform", thPlugin::ARG_IN);
       plugin->setArgValues(args[IN_WAVEFORM], waveforms, 6);

   and an out-of-range index is quietly ignored rather than trusted. module_init
   runs inside dlopen, from a plugin nobody in this tree necessarily wrote. */
void thPlugin::setArgStep (int index, float step)
{
    if (index < 0 || index >= (int)args_.size())
        return;

    args_[index].step = step;
}

void thPlugin::setArgValues (int index, const char *const *names, int count)
{
    if (index < 0 || index >= (int)args_.size() || names == NULL || count <= 0)
        return;

    args_[index].values.clear();
    args_[index].values.reserve(count);

    for (int i = 0; i < count; i++)
    {
        /* A hole in the list is a value with no name, not the end of it:
           osc::window implements 0, 2 and 3 and leaves 1, 4 and 5 out, so its
           list has to be able to say "nothing sensible here" in the middle. */
        args_[index].values.push_back(names[i] ? names[i] : "");
    }

    /* Naming every value says more than a step of 1 does, so it implies one
       rather than needing to be paired with one at every call site. */
    args_[index].step = 1;
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
