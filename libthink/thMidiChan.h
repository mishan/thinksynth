/* $Id: thMidiChan.h,v 1.9 2003/04/26 23:08:25 ink Exp $ */

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
		thBSTree args, notes; 
};

#endif /* TH_MIDICHAN_H */
