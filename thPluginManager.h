#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager();
	~thPluginManager();

	void LoadPlugin(char *name);
	
private:
	thPluginSignaler *signaler;
	thList *plugins;
};

#endif /* TH_PLUGIN_MANAGER_H */
