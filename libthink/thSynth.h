/* $Id: thSynth.h,v 1.25 2003/05/02 19:48:24 ink Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;

class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(const char *name);
	thMod *FindMod(const char *modname);
	void ListMods(void);
	void BuildSynthTree(const char *modname);
	thPluginManager *GetPluginManager (void);
	void AddChannel(char *channame, char *modname, float amp);
	thMidiNote *AddNote(char *channame, float note, float velocity);
	void Process(void);
	void PrintChan(int chan);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);
	
	void ListMods(thBSTree *node);

	void ProcessHelper(thBSTree *chan);

	thBSTree *modlist;
	thPluginManager pluginmanager;
	thList chanlist;
	thBSTree *channels; /* MIDI channels */
	float **output;
	int chans;  /* Number of channels (mono/stereo/etc) */
	int windowlen;
};

#endif /* TH_SYNTH_H */
