/* $Id: thMidiChan.h,v 1.14 2003/04/29 00:16:35 ink Exp $ */

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
		void ProcessHelper (thBSTree *note);
		int GetLen(int);

		thMod *modnode;
		thBSTree *args, *notes;
		int channels, windowlength;
		float **output;
		int outputnamelen;
};

#endif /* TH_MIDICHAN_H */
