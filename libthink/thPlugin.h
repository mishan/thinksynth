#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

class thPlugin {
public:
	thPlugin(const char *path, int id, bool state);
	~thPlugin ();

	const char *GetPath (void);
	const char *GetDesc (void);
	bool GetState (void);

	void MakePath (void);

	void SetDesc(const char *desc);
	void SetState(bool state);

	int Fire (void *node, void *mod);
private:
	char *plugPath;
	int plugId;
	bool plugState;
	void *plugHandle;
	char *plugDesc;

	void (*plugCallback)(void *, void *);

	int ModuleLoad (void);
	void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
