class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(const char *name);
	thMod *FindMod(const char *modname);
	void ListMods(void);
	void BuildSynthTree(const char *modname);
	const thPluginManager *GetPluginManager(void);
	void Process(const char *modname);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	thBSTree modlist;
	thPluginManager pluginmanager;

	unsigned int windowlen;
};
