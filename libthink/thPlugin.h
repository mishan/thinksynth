/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

class thSynthTree;
class thNode;

#define MODULE_IFACE_VER 4

/* We don't want this to exist unless we're using a plugin. */
#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
class thPlugin;

/* Provide the prototypes */
extern "C" {
    int  module_init (thPlugin *plugin);
    int  module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                          unsigned int samples);
    void module_cleanup (struct module *mod);
}
#endif

class thPlugin {
public:
    thPlugin (const string &path);
    ~thPlugin ();

    enum State { ACTIVE, PASSIVE, NOTLOADED };

    typedef int (*Callback)(thNode *,thSynthTree *,unsigned int, unsigned int);
    typedef int (*ModuleInit)(thPlugin *);
    typedef void (*ModuleCleanup)(thPlugin *);
    
    const string &path (void) const { return path_; };
    const string &desc (void) const { return desc_; };
    State state (void) const { return state_; };
    
    void setDesc(const string &desc) { desc_ = desc; }
    void setState(State state) { state_ = state; };
    
    int regArg (const string &argname);
    
    int argCount (void) const { return argcounter_; };
    string getArgName (int index) { 
        if (index >= 0 && index < argcounter_)
            return *args_[index]; 
        return "";
    }
    
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
    int argcounter_; /* how many args are registered */
    int argsize_; /* length of the arg storage array */

    Callback callback_;
};

#endif /* TH_PLUGIN_H */
