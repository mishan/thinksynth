/* $Id: thSynth.h,v 1.42 2004/04/01 09:27:38 misha Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;
class thMidiChan;

class thSynth {
public:
	thSynth (void);
	
	/* XXX: copy constructor is not finished */
	thSynth (thSynth *copySynth);
	~thSynth (void);

	thMod* LoadMod(const string &filename);
	thMod* LoadMod(const string &filename, int channum, float amp);
	thMod* LoadMod(FILE *input);
	thMod *FindMod(const string &name) { return modlist[name]; };
	void ListMods(void);
	void BuildSynthTree(const string &modname);
	thPluginManager *GetPluginManager (void) { return &pluginmanager; };
	void AddChannel(int channum, const string &modname, float amp);

	thMidiNote *AddNote(int channum, float note, float velocity);
	int DelNote (int channum, float note);

	int SetNoteArg (int channum, int note, char *name, float *value, int len);
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

	int GetChannelCount (void) const { return channelcount; }
	map<int, string> *GetPatchlist (void) { return &patchlist; }

	thArg *GetChanArg (int channum, const string &argname);
	void SetChanArg (int channum, thArg *arg);
private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	map<string, thMod*> modlist;
	map<int, string> patchlist;
	thPluginManager pluginmanager;
//	thList chanlist;
	thMidiChan **channels; /* MIDI channels */
	int channelcount;
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
	long thSamples; /* the number of samples per second*/

	pthread_mutex_t *synthMutex;
};

#endif /* TH_SYNTH_H */
