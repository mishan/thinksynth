#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#define MODULE_IFACE_VER 3

/* XXX */
#define PLUGPREFIX "plugins/"
#define PLUGPOSTFIX ".so"

class thPlugin {
public:
	thPlugin(const char *name, int id, bool state);
	~thPlugin ();

	const char *GetName (void);
	const char *GetDesc (void);

	void MakePath (void);

	void SetDesc(const char *desc);

	int Fire (void);
private:
	char *plugName;
	int plugId;
	bool plugState;
	void *plugHandle;
	char *plugDesc;
	char *plugPath;

	int ModuleLoad (void);
	void ModuleUnload (void);
};

#endif /* TH_PLUGIN_H */
