/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

class thMod;
class thNode;

#define MODULE_IFACE_VER 4

/* We don't want this to exist unless we're using a plugin. */
#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
class thPlugin;

/* Provide the prototypes */
extern "C" {
	int  module_init (thPlugin *plugin);
	int  module_callback (thNode *node, thMod *mod, unsigned int windowlen,
						  unsigned int samples);
	void module_cleanup (struct module *mod);
}
#endif

class thPlugin {
public:
	thPlugin (const string &path);
	~thPlugin ();

	enum State { ACTIVE, PASSIVE, NOTLOADED };
	typedef int (*Callback)(thNode *, thMod *, unsigned int, unsigned int);
	typedef int (*ModuleInit)(thPlugin *);
	typedef void (*ModuleCleanup)(thPlugin *);
	
	string getPath (void) const { return plugPath_; };
	string getDesc (void) const { return plugDesc_; };
	State getState (void) const { return plugState_; };
	
	void makePath (void);
	
	void setDesc(const string &desc);
	void setState(State state) { plugState_ = state; };
	
	int regArg (const string &argname);
	
	int getArgs (void) const { return argcounter_; };
	string getArgName (int index) { return *args_[index]; };
	
	void fire (thNode *node, thMod *mod, unsigned int windowlen,
			   unsigned int samples);
private:
	int moduleLoad (void);
	void moduleUnload (void);

	string plugPath_;
	State plugState_;
	void *plugHandle_;
	string plugDesc_;

	string **args_;
	int argcounter_; /* how many args are registered */
	int argsize_; /* length of the arg storage array */

	Callback plugCallback_;
};

#endif /* TH_PLUGIN_H */
