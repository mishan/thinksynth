#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

/* XXX */
#define PLUGPREFIX "plugins/"
#define PLUGPOSTFIX ".so"

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
