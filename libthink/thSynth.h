class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(char *name);
	thMod *FindMod(char *modname);
	void ListMods(void);
	void BuildSynthTree(char *modname);

private:
	int BuildSynthTreeHelper(thMod *mod, char *nodename);

	thBSTree *modlist;
};
