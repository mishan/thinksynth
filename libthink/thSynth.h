class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(char *name);

private:
	thBSTree *modlist;
};
