/* $Id: thPluginManager.h,v 1.11 2004/05/25 03:54:04 misha Exp $ */

#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager(const string &path);
	~thPluginManager();

	int LoadPlugin(const string &name);
	void UnloadPlugin(const string &name);

	thPlugin *GetPlugin (const string &name) { return plugins[name]; };
private:
	map <string, thPlugin*> plugins;

	void UnloadPlugins (void);

	const string GetPath (const string &name);

	string plugin_path;
};

#endif /* TH_PLUGIN_MANAGER_H */
