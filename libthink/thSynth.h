/* $Id: thSynth.h,v 1.33 2003/10/16 21:04:46 misha Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;
class thMidiChan;

class thSynth {
public:
	thSynth (void);
	~thSynth (void);

	void LoadMod(const string &filename);
	void LoadMod(FILE *input);
	thMod *FindMod(const string &name) { return modlist[name]; };
	void ListMods(void);
	void BuildSynthTree(const string &modname);
	const thPluginManager *GetPluginManager (void) const { return &pluginmanager; };
	void AddChannel(const string &channame, const string &modname, float amp);
	thMidiNote *AddNote(const string &channame, float note, float velocity);
	void Process(void);
	void PrintChan(int chan);

	int GetChans(void) const { return thChans; }
	int GetWindowLen(void) const { return thWindowlen; }
	float *GetOutput(void) const { return thOutput; }

	/* note that as of 9/15/03 these don't do anything. corresponding changes
	   elsewhere in the implementation must be made (thSamples is intialized to
	   TH_SAMPLE in the the constructor though, so calls to GetSamples should
	   work ok) (brandon) */
	long GetSamples(void) { return thSamples; }
	void SetSamples(long) { return; }
private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	map<string, thMod*> modlist;
	thPluginManager pluginmanager;
//	thList chanlist;
	map<string, thMidiChan*> channels; /* MIDI channels */
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
	/* this private data member added on 9/15/03 */
	long thSamples; /* the number of samples per second*/
};

#endif /* TH_SYNTH_H */
