/* $Id: thPlugin.h,v 1.24 2004/04/08 13:33:30 ink Exp $ */

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

/* We don't want this to exist unless we're using a plugin. */

#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
class thNode;
class thPlugin;
class thMod;

/* Provide the prototypes */
extern "C" {
	int  module_init (thPlugin *plugin);
	int  module_callback (thNode *node, thMod *mod, unsigned int windowlen);
	void module_cleanup (struct module *mod);
}
#endif

extern string plugin_path;

enum thPluginState { thActive, thPassive, thNotLoaded };

class thNode;
class thMod;

class thPlugin {
public:
	thPlugin(const string &path);
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
	
	void Fire (thNode *node, thMod *mod, unsigned int windowlen);
	
private:
	string plugPath;
	thPluginState plugState;
	void *plugHandle;
	string plugDesc;

	string **args;
	int argcounter; /* how many args are registered */
	int argsize; /* length of the arg storage array */
	
	void (*plugCallback)(thNode *, thMod *, unsigned int);
	
	int ModuleLoad (void);
	void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
