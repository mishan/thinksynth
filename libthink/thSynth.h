/* $Id: thSynth.h,v 1.30 2003/05/30 00:55:42 aaronl Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;
class thMidiChan;

class thSynth {
public:
	thSynth (void);
	~thSynth (void);

	void LoadMod(const char *name);
	thMod *FindMod(const string &name) { return modlist[name]; };
	void ListMods(void);
	void BuildSynthTree(const string &modname);
	const thPluginManager *GetPluginManager (void) const { return &pluginmanager; };
	void AddChannel(const string &channame, const string &modname, float amp);
	thMidiNote *AddNote(const string &channame, float note, float velocity);
	void Process(void);
	void PrintChan(int chan);
	int GetChans(void) { return thChans; }
	int GetWindowLen(void) { return thWindowlen; }
	float *GetOutput(void) { return thOutput; }

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	map<string, thMod*> modlist;
	thPluginManager pluginmanager;
//	thList chanlist;
	map<string, thMidiChan*> channels; /* MIDI channels */
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
};

#endif /* TH_SYNTH_H */
