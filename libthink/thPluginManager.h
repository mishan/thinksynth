/* $Id: thPluginManager.h,v 1.9 2003/04/27 19:59:18 joshk Exp $ */

#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager();
	~thPluginManager();

	int LoadPlugin(char *name);
	void UnloadPlugin(char *name);

	thPlugin *GetPlugin (char *name);
private:
	thBSTree *plugins;

	void UnloadPlugins (void);

	char *GetPath (char *name);
};

#endif /* TH_PLUGIN_MANAGER_H */
