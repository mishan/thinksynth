/* $Id: thPlugin.h,v 1.27 2004/08/16 09:34:48 misha Exp $ */
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

#define MODULE_IFACE_VER 4

/* We don't want this to exist unless we're using a plugin. */

#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
class thNode;
class thPlugin;
class thMod;

/* Provide the prototypes */
extern "C" {
	int  module_init (thPlugin *plugin);
	int  module_callback (thNode *node, thMod *mod, unsigned int windowlen,
						  unsigned int samples);
	void module_cleanup (struct module *mod);
}
#endif

enum thPluginState { thActive, thPassive, thNotLoaded };

class thNode;
class thMod;

typedef int (*th_plugin_callback_t)(thNode *, thMod *, unsigned int,
									unsigned int);

class thPlugin {
public:
	thPlugin (const string &path);
	~thPlugin ();
	
	string GetPath (void) const { return plugPath; };
	string GetDesc (void) const { return plugDesc; };
	thPluginState GetState (void) const { return plugState; };
	
	void MakePath (void);
	
	void SetDesc(const string &desc);
	void SetState(thPluginState state) { plugState = state; };
	
	int RegArg (const string &argname);
	
	int GetArgs (void) const { return argcounter; };
	string GetArgName (int index) { return *args[index]; };
	
	void Fire (thNode *node, thMod *mod, unsigned int windowlen,
			   unsigned int samples);
private:
	string plugPath;
	thPluginState plugState;
	void *plugHandle;
	string plugDesc;

	string **args;
	int argcounter; /* how many args are registered */
	int argsize; /* length of the arg storage array */
	
	th_plugin_callback_t plugCallback;

	int ModuleLoad (void);
	void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
