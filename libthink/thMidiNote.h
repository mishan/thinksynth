/* $Id: thMidiNote.h,v 1.15 2003/05/30 00:55:42 aaronl Exp $ */

#ifndef TH_MIDINOTE_H
#define TH_MIDINOTE_H 1

class thMidiNote {
public:
	thMidiNote(thMod *mod, float note, float velocity);
	thMidiNote(thMod *mod);
	~thMidiNote();
	
	/* takes the arg name, and a pointer to a list of values */
	//	void SetArg (const char *name, float *value, int num);
	
	/* returns a pointer to a list of values */
	//thArgValue *GetArg(const char *name);
	
	thMod *GetMod(void) { return &modnode; }
	int GetID(void) const { return noteid; }

	void Process (int length);

	static void DestroyMap (map<int,thMidiNote*> themap)
	{
		DESTROYBODY(int, thMidiNote);
	};

private:
	thMod modnode;
	int noteid;
};

#endif /* TH_MIDINOTE_H */
