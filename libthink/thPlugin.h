/* $Id: thPlugin.h,v 1.9 2003/04/25 07:18:42 joshk Exp $ */

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

enum thPluginState { thActive, thPassive, thNotLoaded };

class thPlugin {
public:
	thPlugin(const char *path);
	~thPlugin ();

	const char *GetPath (void) { return plugPath; };
	const char *GetDesc (void) { return plugDesc; };
	thPluginState GetState (void) { return plugState; };

	void MakePath (void);

	void SetDesc(const char *desc);
	void SetState(thPluginState state) { plugState = state; };

	int Fire (void *node, void *mod, unsigned int windowlen);
private:
	char *plugPath;
	thPluginState plugState;
	void *plugHandle;
	char *plugDesc;

	void (*plugCallback)(void *, void *, unsigned int);

	int ModuleLoad (void);
	void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
