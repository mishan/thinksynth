/* $Id: thMidiChan.h,v 1.7 2003/04/25 07:18:42 joshk Exp $ */

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
	public:
		thMidiChan (thMod *mod);
		~thMidiChan();

		thMidiNote *AddNote(float note, float velocity);
		void DelNote(thMidiNote *midinote);

		void SetArg(thArg *arg);
	private:
		thMod *modnode;
		thList args, notes; 
};

#endif /* TH_MIDICHAN_H */
