class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(char *name);
	thMod *FindMod(char *modname);
	void ListMods(void);
	void BuildSynthTree(char *modname);
	const thPluginManager *GetPluginManager(void);
	void Process(char *modname);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	thBSTree modlist;
	thPluginManager pluginmanager;

	unsigned int windowlen;
};
