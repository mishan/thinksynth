/* $Id: thMidiChan.h,v 1.22 2004/03/26 06:25:46 misha Exp $ */

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
	
	void SetArg (thArg *arg);
	
	void Process (void);
	
	float *GetOutput (void) const { return output; }
	int GetChannels (void) const { return channels; }

	thMod *GetMod (void) { return modnode; }
	
private:
	int GetLen(int);
	
	bool dirty;
	thMod *modnode;
	map<string, thArg*> args;
	map<int, thMidiNote*> notes;
	int channels, windowlength;
	float *output;
	int outputnamelen;
};

#endif /* TH_MIDICHAN_H */
