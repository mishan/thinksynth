/* $Id: thMidiChan.h,v 1.10 2003/04/27 02:31:43 misha Exp $ */

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
	public:
		thMidiChan (thMod *mod);
		~thMidiChan();

		thMidiNote *AddNote(float note, float velocity);
		void DelNote(int note);

		void SetArg(thArg *arg);

	private:
		thMod *modnode;
		thBSTree *args, *notes; 
};

#endif /* TH_MIDICHAN_H */
