/* $Id: thMidiChan.h,v 1.12 2003/04/28 22:32:48 ink Exp $ */

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
	public:
		thMidiChan (thMod *mod, float amp, int windowlen);
		~thMidiChan();

		thMidiNote *AddNote(float note, float velocity);
		void DelNote(int note);

		void SetArg(thArg *arg);

		void Process (void);

	private:
		int GetLen(int);

		thMod *modnode;
		thBSTree *args, *notes;
		int channels;
		float *output;
		int outputnamelen;
};

#endif /* TH_MIDICHAN_H */
