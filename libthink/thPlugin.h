/* $Id: thPlugin.h,v 1.19 2003/04/30 03:34:33 joshk Exp $ */

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

/* We don't want this to exist unless we're using a plugin. */

#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
#endif

extern char* plugin_path;

enum thPluginState { thActive, thPassive, thNotLoaded };

class thNode;
class thMod;

class thPlugin {
	public:
		thPlugin(const char *path);
		~thPlugin ();

		inline char *GetPath (void) const { return plugPath; };
		inline char *GetDesc (void) const { return plugDesc; };
		thPluginState GetState (void) const { return plugState; };

		void MakePath (void);

		void SetDesc(const char *desc);
		void SetState(thPluginState state) { plugState = state; };
		thPluginState GetState(void) { return plugState; };

		int Fire (thNode *node, thMod *mod, unsigned int windowlen);

	private:
		char *plugPath;
		thPluginState plugState;
		void *plugHandle;
		char *plugDesc;

		void (*plugCallback)(thNode *, thMod *, unsigned int);

		int ModuleLoad (void);
		void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
