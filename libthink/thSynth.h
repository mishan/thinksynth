class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(char *name);
	thMod *FindMod(char *modname);
	void ListMods(void);

private:
	thBSTree *modlist;
};
