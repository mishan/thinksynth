class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(const char *name);
	thMod *FindMod(const char *modname);
	void ListMods(void);
	void BuildSynthTree(const char *modname);
	const thPluginManager *GetPluginManager(void);
	void AddChannel(char *channame, char *modname);
	thMidiNote *AddNote(char *channame, float note, float velocity);
	void Process(const char *modname);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	thBSTree modlist;
	thPluginManager pluginmanager;
	thList chanlist;
	thBSTree channels;

	unsigned int windowlen;
};
