/* $Id: thPlugin.h,v 1.21 2003/05/30 00:55:42 aaronl Exp $ */

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

/* We don't want this to exist unless we're using a plugin. */

#ifdef PLUGIN_BUILD
unsigned char apiversion = MODULE_IFACE_VER;
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

		void Fire (thNode *node, thMod *mod, unsigned int windowlen);

		static void DestroyMap (map<string,thPlugin*> themap)
		{
			DESTROYBODY(string,thPlugin);
		}

	private:
		string plugPath;
		thPluginState plugState;
		void *plugHandle;
		string plugDesc;

		void (*plugCallback)(thNode *, thMod *, unsigned int);

		int ModuleLoad (void);
		void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
