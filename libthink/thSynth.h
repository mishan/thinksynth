class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(char *name);
	void ListMods(void);

private:
	thBSTree *modlist;
};
