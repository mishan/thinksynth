#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
public:
	thMidiChan (thMod *mod);
	~thMidiChan();

	void AddNote(float note, float velocity);
	void DelNote(thMidiNote *midinote);

	void SetArg(thArg *arg);
private:
	thMod *modnode;
	thList args, notes; 
};

#endif /* TH_MIDICHAN_H */
