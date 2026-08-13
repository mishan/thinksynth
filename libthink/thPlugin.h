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

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#include "thExport.h"

class thSynthTree;
class thNode;

#define MODULE_IFACE_VER 4

/* We don't want this to exist unless we're using a plugin.
 *
 * These four are what the host looks up by name, so they are the plugin's
 * entire ABI and the only symbols it needs to export. Declaring them here
 * with THINK_PLUGIN_API means all 66 plugins get the export attribute from
 * the header rather than each having to say so -- on Windows the attribute
 * on a prior declaration is what counts, and every plugin includes think.h
 * before defining them.
 *
 * module_cleanup used to be declared `void module_cleanup(struct module *)'
 * -- and `struct module' is a type that exists nowhere in the tree; it was
 * being forward-declared into existence by that parameter list. Meanwhile
 * thPlugin.cpp casts the looked-up symbol to ModuleCleanup, which is
 * void (*)(thPlugin *), and calls it with `this'. So every plugin's cleanup
 * hook was called through a function pointer of the wrong type on every
 * unload. It survived only because all 66 bodies are empty. The declaration
 * now matches the typedef and the call.
 */
#ifdef PLUGIN_BUILD
THINK_PLUGIN_API unsigned char apiversion = MODULE_IFACE_VER;
class thPlugin;

/* Provide the prototypes */
extern "C" {
    THINK_PLUGIN_API int  module_init (thPlugin *plugin);
    THINK_PLUGIN_API int  module_callback (thNode *node, thSynthTree *mod,
                                           unsigned int windowlen,
                                           unsigned int samples);
    THINK_PLUGIN_API void module_cleanup (thPlugin *plugin);
}
#endif

class THINK_API thPlugin {
public:
    thPlugin (const string &path);
    ~thPlugin ();

    enum State { ACTIVE, PASSIVE, NOTLOADED };

    /* Which way an arg faces.
     *
     * Every plugin has always encoded this in the enum it indexes args[] with
     * -- IN_FREQ, OUT_ARG, INOUT_LAST -- but the information stopped at the
     * enum and never reached anything that could use it. regArg() took only a
     * name, so nothing outside a plugin could tell an input from an output,
     * which is what appeared to block building a node editor.
     *
     * STATE is the INOUT_ case: scratch a plugin keeps between windows --
     * delay rings, envelope positions, filter history. It is emphatically not
     * a port. No .dsp in the shipped corpus references one, and an editor must
     * not offer them for wiring.
     *
     * The default is ARG_IN so that a plugin built against the old header
     * still compiles; its args will all read as inputs, which is wrong but
     * harmless, and every in-tree plugin passes a direction explicitly.
     */
    enum ArgDir { ARG_IN = 0, ARG_OUT, ARG_STATE };

    typedef int (*Callback)(thNode *,thSynthTree *,unsigned int, unsigned int);
    typedef int (*ModuleInit)(thPlugin *);
    typedef void (*ModuleCleanup)(thPlugin *);
    
    const string &path (void) const { return path_; };
    const string &desc (void) const { return desc_; };
    State state (void) const { return state_; };
    
    void setDesc(const string &desc) { desc_ = desc; }
    void setState(State state) { state_ = state; };
    
    int regArg (const string &argname, ArgDir dir = ARG_IN);

    int argCount (void) const { return argcounter_; };
    string getArgName (int index) { 
        if (index >= 0 && index < argcounter_)
            return *args_[index]; 
        return "";
    }

    ArgDir getArgDir (int index) const {
        if (index >= 0 && index < argcounter_)
            return argdirs_[index];
        return ARG_IN;
    }

    /* Convenience for the editor: a port is anything a .dsp may legitimately
       wire, i.e. everything except plugin-internal state. */
    bool argIsPort (int index) const { return getArgDir(index) != ARG_STATE; }
    
    void fire (thNode *node, thSynthTree *mod, unsigned int windowlen,
               unsigned int samples);
private:
    int moduleLoad (void);
    void moduleUnload (void);

    string path_;
    string desc_;
    State state_;
    void *handle_;

    string **args_;
    ArgDir *argdirs_;   /* parallel to args_ */
    int argcounter_; /* how many args are registered */
    int argsize_; /* length of the arg storage array */

    Callback callback_;
};

#endif /* TH_PLUGIN_H */
