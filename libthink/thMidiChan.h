/* $Id: thMidiChan.h,v 1.28 2004/07/30 20:17:40 ink Exp $ */

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
public:
	thMidiChan (thMod *mod, float amp, int windowlen);
	~thMidiChan (void);
	
	thMidiNote *AddNote (float note, float velocity);
	void DelNote (int note);
	
	thMidiNote *GetNote (int note);
	int SetNoteArg (int note, char *name, float *value, int len);
	
	thArg *GetArg (string argName) { return args[argName]; }
	void SetArg (thArg *arg);
	map<string, thArg*> GetArgs (void) { return args; }
	
	void Process (void);
	
	float *GetOutput (void) const { return output; }
	int GetChannels (void) const { return channels; }

	thMod *GetMod (void) { return modnode; }

	void CopyChanArgs (thMod *mod);
	
private:
	int GetLen(int);
	void AssignChanArgPointers(thMod *mod);
	
	bool dirty;
	thMod *modnode;
	map<string, thArg*> args;
	map<int, thMidiNote*> notes;
	list<thMidiNote*> decaying;  /* linked list for decaying notes */
	int channels, windowlength;
	float *output;
	int outputnamelen;
	int polymax;  /* maximum polyphony */
	int notecount, notecount_decay;  /* keeping track of polyphony this way
										for now */
};

#endif /* TH_MIDICHAN_H */
