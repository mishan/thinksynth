#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

class thPlugin {
public:
	thPlugin(char *name, int id, bool state);
	~thPlugin ();

	char *GetName (void);

	int Fire (void);
private:
	char *plugName;
	int plugId;
	bool plugState;
};

#endif /* TH_PLUGIN_H */
