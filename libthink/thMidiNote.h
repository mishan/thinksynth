/* $Id: thMidiNote.h,v 1.7 2003/04/25 07:18:42 joshk Exp $ */

#ifndef TH_MIDINOTE_H
#define TH_MIDINOTE_H 1

class thMidiNote {
public:
	thMidiNote(thMod *mod, float note, float velocity);
	thMidiNote(thMod *mod);
	~thMidiNote();
	
	/* takes the arg name, and a pointer to a list of values */
	void SetArg (const char *name, float *value, int num);

	/* returns a pointer to a list of values */
	thArgValue *GetArg(const char *name);

	void Process (void);
private:
	thBSTree args;
	thMod *modnode;
};

#endif /* TH_MIDINOTE_H */
