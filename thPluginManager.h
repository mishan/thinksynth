#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager();
	~thPluginManager();

private:
	thPluginSignaler *signaler;
};

#endif /* TH_PLUGIN_MANAGER_H */
