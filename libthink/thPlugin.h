/* $Id: thPlugin.h,v 1.26 2004/05/26 00:14:04 misha Exp $ */

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
