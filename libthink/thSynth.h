/* $Id: thSynth.h,v 1.29 2003/05/07 02:28:58 misha Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;

class thSynth {
public:
	thSynth (void);
	~thSynth (void);

	void LoadMod(const char *name);
	thMod *FindMod(const char *modname);
	void ListMods(void);
	void BuildSynthTree(const char *modname);
	thPluginManager *GetPluginManager (void);
	void AddChannel(char *channame, char *modname, float amp);
	thMidiNote *AddNote(char *channame, float note, float velocity);
	void Process(void);
	void PrintChan(int chan);
	int GetChans(void) { return thChans; }
	int GetWindowLen(void) { return thWindowlen; }
	float *GetOutput(void) { return thOutput; }

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);
	
	void ListMods(thBSTree *node);

	void ProcessHelper(thBSTree *chan);

	thBSTree *modlist;
	thPluginManager pluginmanager;
	thList chanlist;
	thBSTree *channels; /* MIDI channels */
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
};

#endif /* TH_SYNTH_H */
