/* $Id: thPluginManager.h,v 1.10 2003/05/30 00:55:42 aaronl Exp $ */

#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager();
	~thPluginManager();

	int LoadPlugin(const string &name);
	void UnloadPlugin(const string &name);

	thPlugin *GetPlugin (const string &name) { return plugins[name]; };
private:
	map <string, thPlugin*> plugins;

	void UnloadPlugins (void);

	const string GetPath (const string &name);
};

#endif /* TH_PLUGIN_MANAGER_H */
