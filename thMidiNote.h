#ifndef TH_MIDINOTE_H
#define TH_MIDINOTE_H 1

class thMidiNote {
public:
	thMidiNote(thMod *mod, float note, float velocity);
	thMidiNote(thMod *mod);
	~thMidiNote();
	
	/* takes the arg name, and a pointer to a list of values */
	void SetArg (char *name, float *value, int num);

	/* returns a pointer to a list of values */
	float *GetArg(char *name);

	void Process (void);
private:
	thList *args;
	thMod *modnode;
};

#endif /* TH_MIDINOTE_H */
